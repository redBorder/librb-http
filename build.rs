extern crate gcc;

fn main() {
    gcc::Config::new()
        .file("src/rb_http_chunked.c")
        .file("src/rb_http_handler.c")
        .file("src/rb_http_normal.c")
        .compile("librbhttp.a");
}
