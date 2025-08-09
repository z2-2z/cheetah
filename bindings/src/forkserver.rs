use libafl::prelude::{Error, ExitKind};
use std::ffi::OsString;
use nix::{
    sys::signal::{Signal, kill},
    unistd::{dup, dup2, close, Pid,},
};
use std::process::{Command, Stdio, Child};
use std::io::{Write, Read, PipeReader, PipeWriter};
use std::os::fd::{FromRawFd, IntoRawFd};

fn create_pipe() -> Result<(i32, i32), Error> {
    let p = std::io::pipe()?;
    let r = p.0.into_raw_fd();
    let w = p.1.into_raw_fd();
    Ok((r, w))
}

const FORKSERVER_MAGIC_MASK: u32 = 0xFFFF0000;
const FORKSERVER_VERSION_MASK: u32 = 0x0000FF00;
const FORKSERVER_MODE_MASK: u32 = 0x000000FF;
const FORKSERVER_MAGIC: u32 = 0xDEAD0000;
const FORKSERVER_FD_ENV_VAR: &str = "__FORKSERVER_FD";

#[repr(u8)]
enum ForkserverCommand {
    Run = 0,
    Stop = 1,
}

#[repr(u8)]
enum ForkserverStatus {
    Exit = 0,
    Crash = 1,
    Timeout = 2,
}

impl From<u8> for ForkserverStatus {
    fn from(value: u8) -> Self {
        match value {
            0 => Self::Exit,
            1 => Self::Crash,
            2 => Self::Timeout,
            _ => panic!("Received invalid forkserver status: {value}"),
        }
    }
}

#[repr(C)]
struct ForkserverConfig {
    timeout: u32,
    signal: u32,
    exit_codes: [u8; 32],
}

impl ForkserverConfig {
    fn new(timeout: u32, signal: u32, exit_codes: &[u8]) -> Self {
        let mut bitmap = [0u8; 32];
        
        for code in exit_codes {
            let idx = *code / 8;
            let bit = 1 << (*code % 8);
            bitmap[idx as usize] |= bit;
        }
        
        Self {
            timeout,
            signal,
            exit_codes: bitmap,
        }
    }
}

#[repr(C)]
#[derive(PartialEq, Debug)]
pub enum ForkserverMode {
    Forkserver = 1,
    Persistent = 2,
}

impl From<u32> for ForkserverMode {
    fn from(value: u32) -> Self {
        match value {
            1 => Self::Forkserver,
            2 => Self::Persistent,
            _ => panic!("Client sent invalid mode in forkserver handshake: {value}"),
        }
    }
}

#[derive(Debug)]
pub struct Forkserver {
    child: Child,
    mode: ForkserverMode,
    pipe: (PipeReader, PipeWriter),
    signal: Signal,
}

impl Forkserver {
    pub fn builder() -> ForkserverBuilder {
        ForkserverBuilder::default()
    }
    
    pub fn mode(&self) -> &ForkserverMode {
        &self.mode
    }
    
    fn handshake(child: Child, pipe: (i32, i32), mut timeout: u32, signal: Signal, crash_exit_codes: Vec<u8>) -> Result<Self, Error> {
        let mut pipe = unsafe {
            (
                PipeReader::from_raw_fd(pipe.0),
                PipeWriter::from_raw_fd(pipe.1),
            )
        };
        
        /* First, check client */
        let mut client_hello = [0u8; 4];
        pipe.0.read_exact(&mut client_hello)?;
        
        let client_hello = u32::from_ne_bytes(client_hello);
        
        if client_hello & FORKSERVER_MAGIC_MASK != FORKSERVER_MAGIC {
            panic!("Mismatching forkserver implementations");
        }
        
        let version = (client_hello & FORKSERVER_VERSION_MASK) >> 8;
        if version != 1 {
            panic!("Invalid forkserver version. Client is on version {version}");
        }
        
        let mode = ForkserverMode::from(client_hello & FORKSERVER_MODE_MASK);
        if mode == ForkserverMode::Persistent && timeout < 1000 {
            timeout = 1000;
        }
        
        /* Then, send config */
        let config = ForkserverConfig::new(timeout, signal as i32 as u32, &crash_exit_codes);
        let buffer = unsafe {
            std::ptr::slice_from_raw_parts(
                std::mem::transmute::<*const ForkserverConfig, *const u8>(&config),
                std::mem::size_of::<ForkserverConfig>(),
            )
        };
        pipe.1.write_all(unsafe { &*buffer })?;
        
        Ok(Self {
            child,
            mode,
            pipe,
            signal,
        })
    }
    
    pub fn launch_target(&mut self) -> Result<(), Error> {
        let buf = [ForkserverCommand::Run as u8];
        self.pipe.1.write_all(&buf)?;
        Ok(())
    }
    
    pub fn collect_status(&mut self) -> Result<ExitKind, Error> {
        let mut buf = [0u8];
        self.pipe.0.read_exact(&mut buf)?;
        
        match ForkserverStatus::from(buf[0]) {
            ForkserverStatus::Exit => Ok(ExitKind::Ok),
            ForkserverStatus::Crash => Ok(ExitKind::Crash),
            ForkserverStatus::Timeout => Ok(ExitKind::Timeout),
        }
    }
    
    pub fn stop_target(&mut self) -> Result<(), Error> {
        let buf = [ForkserverCommand::Stop as u8];
        self.pipe.1.write_all(&buf)?;
        Ok(())
    }
    
    #[inline(always)]
    pub fn execute_target(&mut self) -> Result<ExitKind, Error> {
        self.launch_target()?;
        self.collect_status()
    }
}

pub struct ForkserverBuilder {
    binary: Option<OsString>,
    args: Vec<OsString>,
    env: Vec<(OsString, OsString)>,
    timeout: u32,
    signal: Signal,
    output: bool,
    crash_exit_code: Vec<u8>,
    forkserver_fd: i32,
}

impl Default for ForkserverBuilder {
    fn default() -> Self {
        Self {
            binary: None,
            args: Vec::new(),
            env: Vec::new(),
            timeout: 5000,
            signal: Signal::SIGKILL,
            output: false,
            crash_exit_code: Vec::new(),
            forkserver_fd: -1,
        }
    }
}

impl Drop for Forkserver {
    fn drop(&mut self) {
        // In case of persistent mode: One for grandchild, one for child
        let _ = self.stop_target();
        let _ = self.stop_target();
        
        if kill(Pid::from_raw(self.child.id() as i32), self.signal).is_err() {
            let _ = self.child.kill();
        }
        
        let _ = self.child.try_wait();
    }
}

impl ForkserverBuilder {
    pub fn binary<S: Into<OsString>>(mut self, binary: S) -> Self {
        self.binary = Some(binary.into());
        self
    }
    
    pub fn arg<S: Into<OsString>>(mut self, arg: S) -> Self {
        self.args.push(arg.into());
        self
    }
    
    pub fn env<K: Into<OsString>, V: Into<OsString>>(mut self, key: K, value: V) -> Self {
        self.env.push((key.into(), value.into()));
        self
    }
    
    pub fn timeout_ms(mut self, timeout: u32) -> Self {
        if timeout != 0 {
            self.timeout = timeout;
        }
        self
    }
    
    pub fn kill_signal(mut self, signal: &str) -> Option<Self> {
        if let Ok(signal) = signal.parse::<Signal>() {
            self.signal = signal;
            Some(self)
        } else {
            None
        }
    }
    
    pub fn output(mut self, output: bool) -> Self {
        self.output = output;
        self
    }
    
    pub fn crash_exit_code(mut self, code: u8) -> Self {
        self.crash_exit_code.push(code);
        self
    }
    
    fn setup_pipes(&mut self) -> Result<(i32, i32), Error> {
        let pipe_forward = create_pipe()?;
        let pipe_backward = create_pipe()?;
        
        /* Target-facing fds */
        self.forkserver_fd = dup(pipe_forward.0)?;
        dup2(pipe_backward.1, self.forkserver_fd + 1)?;
        
        unsafe {
            std::env::set_var(FORKSERVER_FD_ENV_VAR, format!("{}", self.forkserver_fd));
        }
        
        close(pipe_forward.0)?;
        close(pipe_backward.1)?;
        
        /* Fuzzer-facing fds */
        Ok((pipe_backward.0, pipe_forward.1))
    }
    
    pub fn spawn(mut self) -> Result<Forkserver, Error> {
        let pipe = self.setup_pipes()?;
        let binary = self.binary.expect("No binary given to forkserver");
        
        let mut command = Command::new(binary);
        command.args(self.args);
        
        if self.output {
            command.stdout(Stdio::inherit());
            command.stderr(Stdio::inherit());
        } else {
            command.stdout(Stdio::null());
            command.stderr(Stdio::null());
        }
        
        let mut has_ld_bind_now = false;
        let mut has_lsan_options = false;
        let mut has_asan_options = false;
        
        for (key, _) in &self.env {
            match key.as_os_str().to_str() {
                Some("LD_BIND_NOW") => has_ld_bind_now = true,
                Some("LSAN_OPTIONS") => has_lsan_options = true,
                Some("ASAN_OPTIONS") => has_asan_options = true,
                _ => {},
            }
        }
        
        command.envs(self.env);
        
        if !has_ld_bind_now && std::env::var("LD_BIND_NOW").is_err() {
            command.env("LD_BIND_NOW", "1");
        }
        if !has_lsan_options && std::env::var("LSAN_OPTIONS").is_err() {
            command.env("LSAN_OPTIONS", "exitcode=23");
        }
        if !has_asan_options && std::env::var("ASAN_OPTIONS").is_err() {
            command.env("ASAN_OPTIONS", "detect_leaks=1:abort_on_error=1:halt_on_error=1:symbolize=0:detect_stack_use_after_return=1:max_malloc_fill_size=1073741824");
        }
        
        let handle = command.spawn()?;
        
        close(self.forkserver_fd)?;
        close(self.forkserver_fd + 1)?;
        
        Forkserver::handshake(handle, pipe, self.timeout, self.signal, self.crash_exit_code)
    }
}


#[cfg(test)]
mod tests {
    use super::*;
    use libafl::prelude::*;
    use libafl_bolts::prelude::*;
    use std::borrow::Cow;
    
    const BINARY: &str = "../tests/hybrid-client";
    const SEED: u64 = 1234;
    
    #[derive(Default, Debug)]
    struct NopMutator;
    
    impl Named for NopMutator {
        fn name(&self) -> &Cow<'static, str> {
            static NAME: Cow<'static, str> = Cow::Borrowed("NopMutator");
            &NAME
        }
    }
    
    impl<I, S> Mutator<I, S> for NopMutator {
        fn mutate(&mut self, _state: &mut S, _input: &mut I) -> Result<MutationResult, Error> {
            Ok(MutationResult::Mutated)
        }
    
        fn post_exec(&mut self, _state: &mut S, _new_corpus_id: Option<CorpusId>) -> Result<(), Error> {
            Ok(())
        }
    }
    
    #[test]
    fn bench_libruntime() -> Result<(), Error> {
        let cores = std::env::var("CORES").unwrap_or_else(|_| "0".to_string());
        let cores = Cores::from_cmdline(&cores)?;
        let map_size = std::cmp::max(64, crate::get_afl_map_size(BINARY)?);
        
        let mut run_client_text = |state: Option<_>, mut mgr: LlmpRestartingEventManager<_, _, _, _, _>, _client: ClientDescription| {
            let mut shmem_provider = UnixShMemProvider::new()?;
            let mut covmap = shmem_provider.new_shmem(map_size)?;
            unsafe {
                covmap.write_to_env("__AFL_SHM_ID")?;
            }
            unsafe {
                std::env::set_var("AFL_MAP_SIZE", format!("{map_size}"));
            }

            let edges_observer = unsafe {
                HitcountsMapObserver::new(StdMapObserver::new("edges", covmap.as_slice_mut())).track_indices()
            };

            let mut feedback = MaxMapFeedback::new(&edges_observer);
            
            let mut objective = feedback_and_fast!(
                feedback_or!(
                    CrashFeedback::new(),
                    TimeoutFeedback::new()
                ),
                MaxMapFeedback::with_name("edges_objective", &edges_observer)
            );
            
            let mut state = if let Some(state) = state { 
                state
            } else {
                StdState::new(
                    StdRand::with_seed(SEED),
                    InMemoryCorpus::<BytesInput>::new(),
                    InMemoryCorpus::new(),
                    &mut feedback,
                    &mut objective,
                )?
            };
            
            let mutational_stage = StdMutationalStage::new(NopMutator);
            
            let scheduler = QueueScheduler::new();
            
            let mut fuzzer = StdFuzzer::new(scheduler, feedback, objective);
            
            let mut forkserver = super::Forkserver::builder()
                .binary(BINARY)
                .arg("libruntime")
                .env("LD_LIBRARY_PATH", "..")
                .timeout_ms(60_000)
                .kill_signal("SIGKILL").unwrap()
                .output(true)
                .spawn()?;
            let mut func = |input: &BytesInput| {
                assert!(input.is_empty());
                forkserver.execute_target().unwrap()
            };
            let mut executor = InProcessExecutor::new(
                &mut func,
                tuple_list!(edges_observer),
                &mut fuzzer,
                &mut state,
                &mut mgr,
            )?;
            
            if state.must_load_initial_inputs() {
                fuzzer.add_input(
                    &mut state,
                    &mut executor,
                    &mut mgr,
                    BytesInput::new(Vec::new()),
                )?;
            }
            
            let mut stages = tuple_list!(mutational_stage);

            fuzzer.fuzz_loop(&mut stages, &mut executor, &mut state, &mut mgr)?;
            
            Ok(())
        };
        
        let monitor = MultiMonitor::new(|s| println!("{s}"));
        let shmem_provider = StdShMemProvider::new()?;

        match Launcher::builder()
            .shmem_provider(shmem_provider)
            .configuration(EventConfig::AlwaysUnique)
            .monitor(monitor)
            .run_client(&mut run_client_text)
            .cores(&cores)
            .build()
            .launch()
        {
            Err(Error::ShuttingDown) | Ok(()) => Ok(()),
            e => e,
        }
    }
    
    #[test]
    fn bench_afl() -> Result<(), Error> {
        const MAP_SIZE: usize = 65535;
        let cores = std::env::var("CORES").unwrap_or_else(|_| "0".to_string());
        let cores = Cores::from_cmdline(&cores)?;
        
        let mut run_client_text = |state: Option<_>, mut mgr: LlmpRestartingEventManager<_, _, _, _, _>, _client: ClientDescription| {
            let mut shmem_provider = UnixShMemProvider::new()?;
            let mut covmap = shmem_provider.new_shmem(MAP_SIZE)?;
            unsafe {
                covmap.write_to_env("__AFL_SHM_ID")?;
            }

            let edges_observer = unsafe {
                HitcountsMapObserver::new(StdMapObserver::new("edges", covmap.as_slice_mut())).track_indices()
            };

            let mut feedback = MaxMapFeedback::new(&edges_observer);
            
            let mut objective = feedback_and_fast!(
                feedback_or!(
                    CrashFeedback::new(),
                    TimeoutFeedback::new()
                ),
                MaxMapFeedback::with_name("edges_objective", &edges_observer)
            );
            
            let mut state = if let Some(state) = state { 
                state
            } else {
                StdState::new(
                    StdRand::with_seed(SEED),
                    InMemoryCorpus::<BytesInput>::new(),
                    InMemoryCorpus::new(),
                    &mut feedback,
                    &mut objective,
                )?
            };
            
            let mutational_stage = StdMutationalStage::new(NopMutator);
            
            let scheduler = QueueScheduler::new();
            
            let mut fuzzer = StdFuzzer::new(scheduler, feedback, objective);
            
            let mut executor = ForkserverExecutor::builder()
                .program(BINARY)
                .arg("afl++")
                .env("LD_LIBRARY_PATH", "..")
                .is_persistent(true)
                .debug_child(true)
                .input(InputLocation::StdIn { input_file: None })
                .coverage_map_size(MAP_SIZE)
                .min_input_size(0)
                .build_dynamic_map(edges_observer, tuple_list!())?;
            
            if state.must_load_initial_inputs() {
                fuzzer.add_input(
                    &mut state,
                    &mut executor,
                    &mut mgr,
                    BytesInput::new(Vec::new()),
                )?;
            }
            
            let mut stages = tuple_list!(mutational_stage);

            fuzzer.fuzz_loop(&mut stages, &mut executor, &mut state, &mut mgr)?;
            
            Ok(())
        };
        
        let monitor = MultiMonitor::new(|s| println!("{s}"));
        let shmem_provider = StdShMemProvider::new()?;

        match Launcher::builder()
            .shmem_provider(shmem_provider)
            .configuration(EventConfig::AlwaysUnique)
            .monitor(monitor)
            .run_client(&mut run_client_text)
            .cores(&cores)
            .build()
            .launch()
        {
            Err(Error::ShuttingDown) | Ok(()) => Ok(()),
            e => e,
        }
    }
}
