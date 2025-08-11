use libafl::prelude::{Error};
use libafl_bolts::prelude::{UnixShMem, UnixShMemProvider, ShMemProvider, ShMem};

const MAX_MESSAGE_SIZE: usize = 64;

#[allow(dead_code)]
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
    
    #[inline]
    fn read_byte(&mut self) -> Result<u8, Error> {
        unsafe {
            if libc::sem_wait(&mut self.semaphore as *mut libc::sem_t) == -1 {
                return Err(Error::last_os_error("Could not read from channel"));
            }
        }
        
        Ok(self.message[0])
    }
    
    #[inline]
    fn write_byte(&mut self, byte: u8) -> Result<(), Error> {
        self.message[0] = byte;
        
        unsafe {
            if libc::sem_post(&mut self.semaphore as *mut libc::sem_t) == -1 {
                return Err(Error::last_os_error("Could not write to channel"));
            }
        }
        
        Ok(())
    }
}

#[repr(C)]
struct IPCChannels {
    command_channel: Channel,
    status_channel: Channel,
}

#[derive(Debug)]
pub(crate) struct ForkserverIPC {
    shmem: UnixShMem,
    
    #[allow(dead_code)]
    last_op: Op,
}

impl ForkserverIPC {
    pub(crate) fn new() -> Result<Self, Error> {
        let mut shmem_provider = UnixShMemProvider::new()?;
        let mut shmem = shmem_provider.new_shmem(4096)?;
        unsafe {
            shmem.write_to_env("__FORKSERVER_SHM")?;
        }
            
        let channels = unsafe { &mut *shmem.as_mut_ptr_of::<IPCChannels>().unwrap_unchecked() };
        channels.command_channel.init()?;
        channels.status_channel.init()?;
        
        Ok(Self {
            shmem,
            last_op: Op::None,
        })
    }
    
    #[allow(dead_code)]
    #[inline]
    fn check_op(&mut self, op: Op) {
        if self.last_op != op {
            self.last_op = op;
        } else {
            panic!("Non-alternating IPC channel ops");
        }
    }
    
    pub(crate) fn read(&mut self, buffer: &mut [u8]) -> Result<(), Error> {
        #[cfg(debug_assertions)]
        self.check_op(Op::Read);
        
        let channels = unsafe { &mut *self.shmem.as_mut_ptr_of::<IPCChannels>().unwrap_unchecked() };
        channels.status_channel.read(buffer)
    }
    
    pub(crate) fn write(&mut self, data: &[u8]) -> Result<(), Error> {
        #[cfg(debug_assertions)]
        self.check_op(Op::Write);
        
        let channels = unsafe { &mut *self.shmem.as_mut_ptr_of::<IPCChannels>().unwrap_unchecked() };
        channels.command_channel.write(data)
    }
    
    pub(crate) fn write_unchecked(&mut self, data: &[u8]) -> Result<(), Error> {
        let channels = unsafe { &mut *self.shmem.as_mut_ptr_of::<IPCChannels>().unwrap_unchecked() };
        channels.command_channel.write(data)
    }
    
    pub(crate) fn post_handshake(&mut self) {
        // Every write from now on will only be one byte so it suffices to the length only once, here
        let channels = unsafe { &mut *self.shmem.as_mut_ptr_of::<IPCChannels>().unwrap_unchecked() };
        channels.command_channel.message_size = 1;
        assert_eq!(channels.status_channel.message_size, 1);
    }
    
    #[inline]
    pub(crate) fn recv_status(&mut self) -> Result<u8, Error> {
        #[cfg(debug_assertions)]
        self.check_op(Op::Read);
        
        let channels = unsafe { &mut *self.shmem.as_mut_ptr_of::<IPCChannels>().unwrap_unchecked() };
        channels.status_channel.read_byte()
    }
    
    #[inline]
    pub(crate) fn send_command(&mut self, cmd: u8) -> Result<(), Error> {
        #[cfg(debug_assertions)]
        self.check_op(Op::Write);
        
        let channels = unsafe { &mut *self.shmem.as_mut_ptr_of::<IPCChannels>().unwrap_unchecked() };
        channels.command_channel.write_byte(cmd)
    }
}
