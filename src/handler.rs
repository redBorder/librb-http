use bytes::Bytes;
use futures::Future;
use hyper::Method;
use hyper::{Request, Response, Uri};
use hyper::client::Client;
use hyper::header::ContentType;
use std::thread;
use std::sync::mpsc;
use std::sync::mpsc::{Sender, Receiver};
use tokio_core::reactor::Core;

pub struct Handler {
    url: Uri,
    tx: Option<Sender<Bytes>>,
    join_handler: Option<thread::JoinHandle<()>>,
}

impl Handler {
    pub fn new() -> Self {
        Handler {
            url: Uri::default(),
            tx: None,
            join_handler: None,
        }
    }

    pub fn url(mut self, url: &str) -> Self {
        self.url = url.parse().expect("Invalid URL");
        self
    }

    pub fn report_handler(mut self, handler: Box<Fn(Response)>) -> Self {
        unimplemented!();
    }

    pub fn run(&mut self) {
        let (tx, rx): (Sender<Bytes>, Receiver<Bytes>) = mpsc::channel();
        self.tx = Some(tx);

        let url = self.url.clone();

        self.join_handler = Some(thread::spawn(move || {
            let mut core = Core::new().unwrap();
            let client = Client::new(&core.handle());

            for message in rx {
                let mut req = Request::new(Method::Post, url.clone());
                req.headers_mut().set(ContentType::json());
                req.set_body(message);

                let post = client.request(req).map(|res| {
                    println!("POST: {}", res.status());
                });

                core.run(post).expect("Error on request");
            }
        }));
    }

    pub fn produce(&self, data: Bytes) {
        self.tx.clone().unwrap().send(data).expect(
            "Error on produce",
        );
    }

    pub fn terminate(&mut self) {
        let tx = self.tx.take();
        drop(tx);

        let join_handler = self.join_handler.take();
        join_handler.unwrap().join().expect(
            "Error waiting for worker to terminate",
        );
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use bytes::{BytesMut, BufMut};

    #[test]
    fn create_handler() {
        let mut handler = Handler::new().url("http://localhost:8080");
        handler.run();

        let mut data = BytesMut::with_capacity(1024);
        data.put(&r##"{"message": "hello world"}"##);
        let data = data.freeze();
        handler.produce(data.clone());

        handler.terminate();
    }
}
