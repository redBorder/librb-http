use libc::{size_t, c_int, c_void, c_char, c_uchar, memcpy};
use std::ffi::CString;
use std::ffi::CStr;
use std::ptr;
use handler::Handler;
use std::slice;
use bytes::{BytesMut, BufMut};

#[no_mangle]
pub extern "C" fn rb_http_handler_create(
    urls_str: *const c_char,
    err: *mut c_char,
    errsize: size_t,
) -> *const Handler {
    let url = unsafe { CStr::from_ptr(urls_str).to_str() };

    match url {
        Ok(url) => {
            let handler = Handler::new().url(url);
            Box::into_raw(Box::new(handler))
        }
        Err(_) => {
            unsafe {
                let error_str = CString::new("Error").unwrap().into_raw();
                memcpy(err as *mut c_void, error_str as *mut c_void, errsize);
            };

            ptr::null()
        }
    }
}

#[no_mangle]
pub extern "C" fn rb_http_handler_destroy(
    rb_http_handler: *mut Handler,
    err: *const c_void,
    errsize: size_t,
) {
    if rb_http_handler.is_null() {
        return;
    }

    unsafe {
        Box::from_raw(rb_http_handler).terminate();
    }
}

#[no_mangle]
pub extern "C" fn rb_http_handler_run(rb_http_handler: *mut Handler) {
    let rb_http_handler = unsafe {
        assert!(!rb_http_handler.is_null());
        &mut *rb_http_handler
    };

    rb_http_handler.run();
}

#[no_mangle]
pub extern "C" fn rb_http_produce(
    ptr: *mut Handler,
    buff: *const c_uchar,
    len: size_t,
    flags: c_int,
    err: *const c_void,
    errsize: size_t,
    opaque: *const c_void,
) -> c_int {
    let http_handler = unsafe {
        assert!(!ptr.is_null());
        &mut *ptr
    };

    let buff = unsafe {
        assert!(!buff.is_null());
        slice::from_raw_parts(buff, len)
    };

    let mut bytes = BytesMut::with_capacity(len);
    bytes.put(&buff);
    http_handler.produce(bytes.freeze());
    0
}
