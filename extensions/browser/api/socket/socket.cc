// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "extensions/browser/api/socket/socket.h"

#include "base/bind.h"
#include "base/lazy_instance.h"
#include "extensions/browser/api/api_resource_manager.h"
#include "net/base/address_list.h"
#include "net/base/io_buffer.h"
#include "net/base/ip_address.h"
#include "net/base/ip_endpoint.h"
#include "net/base/net_errors.h"
#include "net/socket/socket.h"

namespace extensions {

const char kSocketTypeNotSupported[] = "Socket type does not support this API";

static base::LazyInstance<
    BrowserContextKeyedAPIFactory<ApiResourceManager<Socket>>>::DestructorAtExit
    g_factory = LAZY_INSTANCE_INITIALIZER;

// static
template <>
BrowserContextKeyedAPIFactory<ApiResourceManager<Socket> >*
ApiResourceManager<Socket>::GetFactoryInstance() {
  return g_factory.Pointer();
}

Socket::Socket(const std::string& owner_extension_id)
    : ApiResource(owner_extension_id), is_connected_(false) {}

Socket::~Socket() {
  // Derived destructors should make sure the socket has been closed.
  DCHECK(!is_connected_);
}

void Socket::Write(scoped_refptr<net::IOBuffer> io_buffer,
                   int byte_count,
                   const net::CompletionCallback& callback) {
  DCHECK(!callback.is_null());
  write_queue_.push(WriteRequest(io_buffer, byte_count, callback));
  WriteData();
}

void Socket::WriteData() {
  // IO is pending.
  if (io_buffer_write_.get())
    return;

  WriteRequest& request = write_queue_.front();

  DCHECK(request.byte_count >= request.bytes_written);
  io_buffer_write_ = base::MakeRefCounted<net::WrappedIOBuffer>(
      request.io_buffer->data() + request.bytes_written);
  int result = WriteImpl(
      io_buffer_write_.get(), request.byte_count - request.bytes_written,
      base::Bind(&Socket::OnWriteComplete, base::Unretained(this)));

  if (result != net::ERR_IO_PENDING)
    OnWriteComplete(result);
}

void Socket::OnWriteComplete(int result) {
  io_buffer_write_ = NULL;

  WriteRequest& request = write_queue_.front();

  if (result >= 0) {
    request.bytes_written += result;
    if (request.bytes_written < request.byte_count) {
      WriteData();
      return;
    }
    DCHECK(request.bytes_written == request.byte_count);
    result = request.bytes_written;
  }

  request.callback.Run(result);
  write_queue_.pop();

  if (!write_queue_.empty())
    WriteData();
}

void Socket::SetKeepAlive(bool enable,
                          int delay,
                          SetKeepAliveCallback callback) {
  std::move(callback).Run(false);
}

void Socket::SetNoDelay(bool no_delay, SetNoDelayCallback callback) {
  std::move(callback).Run(false);
}

void Socket::Listen(const std::string& address,
                    uint16_t port,
                    int backlog,
                    ListenCallback callback) {
  std::move(callback).Run(net::ERR_FAILED, kSocketTypeNotSupported);
}

void Socket::Accept(AcceptCompletionCallback callback) {
  std::move(callback).Run(net::ERR_FAILED, nullptr /* socket */, base::nullopt,
                          mojo::ScopedDataPipeConsumerHandle(),
                          mojo::ScopedDataPipeProducerHandle());
}

// static
bool Socket::StringAndPortToIPEndPoint(const std::string& ip_address_str,
                                       uint16_t port,
                                       net::IPEndPoint* ip_end_point) {
  DCHECK(ip_end_point);
  net::IPAddress ip_address;
  if (!ip_address.AssignFromIPLiteral(ip_address_str))
    return false;

  *ip_end_point = net::IPEndPoint(ip_address, port);
  return true;
}

void Socket::IPEndPointToStringAndPort(const net::IPEndPoint& address,
                                       std::string* ip_address_str,
                                       uint16_t* port) {
  DCHECK(ip_address_str);
  DCHECK(port);
  *ip_address_str = address.ToStringWithoutPort();
  if (ip_address_str->empty()) {
    *port = 0;
  } else {
    *port = address.port();
  }
}

Socket::WriteRequest::WriteRequest(scoped_refptr<net::IOBuffer> io_buffer,
                                   int byte_count,
                                   const net::CompletionCallback& callback)
    : io_buffer(io_buffer),
      byte_count(byte_count),
      callback(callback),
      bytes_written(0) {}

Socket::WriteRequest::WriteRequest(const WriteRequest& other) = default;

Socket::WriteRequest::~WriteRequest() {}

// static
net::NetworkTrafficAnnotationTag Socket::GetNetworkTrafficAnnotationTag() {
  return net::DefineNetworkTrafficAnnotation("chrome_apps_socket_api", R"(
        semantics {
          sender: "Chrome Apps Socket API"
          description:
            "Chrome Apps can use this API to send and receive data over "
            "the network using TCP and UDP connections."
          trigger: "A request from a Chrome App."
          data: "Any data that the app sends."
          destination: OTHER
          destination_other:
            "Data can be sent to any destination included in the app manifest."
        }
        policy {
          cookies_allowed: NO
          setting:
            "No settings control. Chrome Connectivity Diagnostics component "
            "uses this API. Other than that, this request will not be sent if "
            "the user does not install a Chrome App that uses the Socket API."
          chrome_policy {
            ExtensionInstallBlacklist {
              ExtensionInstallBlacklist: {
                entries: '*'
              }
            }
          }
        })");
}

}  // namespace extensions
