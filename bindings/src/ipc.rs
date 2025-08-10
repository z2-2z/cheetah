use libafl::prelude::{Error};
use libafl_bolts::prelude::{UnixShMem, UnixShMemProvider, ShMemProvider, ShMem};

const MAX_MESSAGE_SIZE: usize = 64;

#[derive(PartialEq, Eq, Debug)]
enum Op {
    None,
    Read,
    Write,
}

#[repr(C)]
struct Channel {
    semaphore: libc::sem_t,
    message_size: usize,
    message: [u8; MAX_MESSAGE_SIZE],
}

impl Channel {
    fn init(&mut self) -> Result<(), Error> {
        unsafe {
            if libc::sem_init(&mut self.semaphore as *mut libc::sem_t, 1, 0) == -1 {
                return Err(Error::last_os_error("Could not initialize semaphore"));
            }
        }
        
        self.message_size = 0;
        
        Ok(())
    }
    
    fn read(&mut self, buffer: &mut [u8]) -> Result<(), Error> {
        unsafe {
            if libc::sem_wait(&mut self.semaphore as *mut libc::sem_t) == -1 {
                return Err(Error::last_os_error("Could not read from channel"));
            }
        }
        
        let len = self.message_size;
        
        if len != buffer.len() {
            return Err(Error::unknown("IPC channel received bytes do not match requested amount"));
        }
        
        buffer.copy_from_slice(&self.message[..len]);
        Ok(())
    }
    
    fn write(&mut self, data: &[u8]) -> Result<(), Error> {
        let len = data.len();
        
        if len > MAX_MESSAGE_SIZE {
            return Err(Error::unknown("Message too large for IPC channel"));
        }
        
        self.message_size = len;
        self.message[..len].copy_from_slice(data);
        
        unsafe {
            if libc::sem_post(&mut self.semaphore as *mut libc::sem_t) == -1 {
                return Err(Error::last_os_error("Could not write to channel"));
            }
        }
        
        Ok(())
    }
}

#[repr(C)]
struct Ipc {
    command_channel: Channel,
    status_channel: Channel,
    // fuzzer does not need last_op
}

#[derive(Debug)]
pub(crate) struct FuzzerIPC {
    shmem: UnixShMem,
    last_op: Op,
}

impl FuzzerIPC {
    pub(crate) fn new() -> Result<Self, Error> {
        let mut shmem_provider = UnixShMemProvider::new()?;
        let mut shmem = shmem_provider.new_shmem(4096)?;
        unsafe {
            shmem.write_to_env("__FORKSERVER_SHM")?;
            
            let ipc = &mut *shmem.as_mut_ptr_of::<Ipc>().unwrap_unchecked();
            ipc.command_channel.init()?;
            ipc.status_channel.init()?;
        }
        Ok(Self {
            shmem,
            last_op: Op::None,
        })
    }
    
    #[inline]
    fn check_op(&mut self, op: Op) -> Result<(), Error> {
        if self.last_op != op {
            self.last_op = op;
            Ok(())
        } else {
            Err(Error::unknown("Non-alternating IPC channel ops"))
        }
    }
    
    pub(crate) fn read(&mut self, buffer: &mut [u8]) -> Result<(), Error> {
        self.check_op(Op::Read)?;
        
        unsafe {
            let ipc = &mut *self.shmem.as_mut_ptr_of::<Ipc>().unwrap_unchecked();
            ipc.status_channel.read(buffer)
        }
    }
    
    pub(crate) fn write(&mut self, data: &[u8]) -> Result<(), Error> {
        self.check_op(Op::Write)?;
        
        unsafe {
            let ipc = &mut *self.shmem.as_mut_ptr_of::<Ipc>().unwrap_unchecked();
            ipc.command_channel.write(data)
        }
    }
    
    pub(crate) fn write_unchecked(&mut self, data: &[u8]) -> Result<(), Error> {
        unsafe {
            let ipc = &mut *self.shmem.as_mut_ptr_of::<Ipc>().unwrap_unchecked();
            ipc.command_channel.write(data)
        }
    }
}
