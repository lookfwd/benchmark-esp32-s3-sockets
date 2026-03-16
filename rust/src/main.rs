use std::env;
use std::io::Read;
use std::net::TcpStream;
use std::time::Instant;

const DEFAULT_ADDR: &str = "192.168.1.100:5000";
const BUF_SIZE: usize = 65536;

fn main() {
    let addr = env::args().nth(1).unwrap_or_else(|| DEFAULT_ADDR.to_string());

    println!("Connecting to {addr}...");
    let mut stream = TcpStream::connect(&addr).expect("Failed to connect");
    println!("Connected!");

    let mut buf = [0u8; BUF_SIZE];
    let mut bytes_this_second: u64 = 0;
    let mut interval_start = Instant::now();

    loop {
        match stream.read(&mut buf) {
            Ok(0) => {
                println!("Connection closed by server");
                break;
            }
            Ok(n) => {
                bytes_this_second += n as u64;
                let elapsed = interval_start.elapsed();
                if elapsed.as_secs_f64() >= 1.0 {
                    let mbps = (bytes_this_second as f64 * 8.0) / (elapsed.as_secs_f64() * 1_000_000.0);
                    let mb_s = bytes_this_second as f64 / (elapsed.as_secs_f64() * 1_000_000.0);
                    println!("{mb_s:.2} MB/s ({mbps:.2} Mbps)");
                    bytes_this_second = 0;
                    interval_start = Instant::now();
                }
            }
            Err(e) => {
                eprintln!("Read error: {e}");
                break;
            }
        }
    }
}
