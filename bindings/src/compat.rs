use std::path::Path;
use std::fs::File;
use std::io::{Result, Error, ErrorKind};
use goblin::Object;

pub fn get_afl_map_size<P: AsRef<Path>>(binary: P) -> Result<usize> {
    let file = File::open(binary)?;
    let mmap = unsafe { memmap2::Mmap::map(&file) }?;
    let Ok(Object::Elf(elf)) = Object::parse(&mmap) else { 
        return Err(Error::new(ErrorKind::InvalidData, "Fuzz target is not an ELF file"));
    };
    
    for sh in &elf.section_headers {
        let name = elf.shdr_strtab.get_at(sh.sh_name);
        
        if matches!(name, Some("__sancov_guards")) {
            /*  According to __sanitizer_cov_trace_pc_guard_init() in
                https://github.com/AFLplusplus/AFLplusplus/blob/stable/instrumentation/afl-compiler-rt.o.c
                AFL++ skips the first 5 counters 
             */
            return Ok(5 + sh.sh_size as usize / 4);
        }
    }
    
    Err(Error::new(ErrorKind::InvalidData, "Fuzz target seems to be stripped"))
}
