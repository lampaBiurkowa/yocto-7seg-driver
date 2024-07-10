use std::fs::{File, OpenOptions};
use std::io::{self, prelude::*, BufReader, Write};
use std::env;

const DEVICE_PATH: &str = "/dev/dib7seg";

fn read_value() -> io::Result<String> {
    let file = File::open(DEVICE_PATH)?;
    let mut reader = BufReader::new(file);
    let mut buffer = String::new();
    reader.read_line(&mut buffer)?;
    Ok(buffer.trim().to_string())
}

fn write_value(value: &str) -> io::Result<()> {
    let mut file = OpenOptions::new().write(true).open(DEVICE_PATH)?;
    file.write_all(value.as_bytes())?;
    Ok(())
}

fn main() -> io::Result<()> {
    let args: Vec<String> = env::args().collect();

    if args.len() < 2 {
        println!("Too few arguments");
        return Ok(());
    }

    match args[1].as_str() {
        "read" => {
            let value = read_value()?;
            println!("Current value: {}", value);
        }
        "write" => {
            if args.len() < 3 {
                println!("Too few arguments");
                return Ok(());
            } else {
                write_value(&args[2])?;
                println!("Value written: {}", args[2]);
            }
        }
        _ => println!("Unknown"),
    }

    Ok(())
}
