use std::io::{self, stdout, Read};

use termion::raw::IntoRawMode;

/// Convert keyboard input <char> to Ctrl-<char>,
fn to_ctrl_byte(c: char) -> u8 {
    let byte = c as u8;
    // ASCII Ctrl characters have bits 0-2 as 0
    byte & 0b0001_1111
}

fn die(e: std::io::Error) {
    panic!("{}", e);
}

fn main() {
    let _stdout = stdout().into_raw_mode().unwrap();

    for b in io::stdin().bytes() {
        match b {
            Ok(b) => {
                let c = b as char;
                if c.is_control() {
                    println!("{:?} \r", b);
                } else {
                    println!("{:?} ({})\r", b, c);
                }
                if b == to_ctrl_byte('c') {
                    break;
                }
            }
            Err(err) => die(err),
        }
    }
}
