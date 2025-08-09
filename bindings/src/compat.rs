use std::path::Path;
use std::io::Result;

pub fn get_afl_map_size<P: AsRef<Path>>(binary: P) -> Result<usize> {
    let file = std::fs::File::open(binary)?;
    let mmap = unsafe { memmap2::Mmap::map(&file) }?;
    let goblin::Object::Elf(elf) = goblin::Object::parse(&mmap)
        .expect("Fuzz target not an ELF file") else { 
        panic!("Fuzz target not an ELF file") 
    };
    
    for sh in &elf.section_headers {
        let name = elf.shdr_strtab.get_at(sh.sh_name);
        
        if matches!(name, Some("__sancov_guards")) {
            return Ok(5 + sh.sh_size as usize / 4);
        }
    }
    
    // Default map size
    Ok(65535)
}
