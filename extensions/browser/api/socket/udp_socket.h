// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef EXTENSIONS_BROWSER_API_SOCKET_UDP_SOCKET_H_
#define EXTENSIONS_BROWSER_API_SOCKET_UDP_SOCKET_H_

#include <stdint.h>

#include <string>
#include <vector>

#include "base/containers/span.h"
#include "base/optional.h"
#include "extensions/browser/api/socket/socket.h"
#include "mojo/public/cpp/bindings/binding.h"
#include "services/network/public/mojom/network_service.mojom.h"
#include "services/network/public/mojom/udp_socket.mojom.h"

namespace extensions {

class UDPSocket : public Socket, public network::mojom::UDPSocketReceiver {
 public:
  UDPSocket(network::mojom::UDPSocketPtrInfo socket,
            network::mojom::UDPSocketReceiverRequest receiver_request,
            const std::string& owner_extension_id);
  ~UDPSocket() override;

  // Socket implementation.
  void Connect(const net::AddressList& address,
               net::CompletionOnceCallback callback) override;
  void Disconnect(bool socket_destroying) override;
  void Bind(const std::string& address,
            uint16_t port,
            net::CompletionOnceCallback callback) override;
  void Read(int count, ReadCompletionCallback callback) override;
  void RecvFrom(int count, RecvFromCompletionCallback callback) override;
  void SendTo(scoped_refptr<net::IOBuffer> io_buffer,
              int byte_count,
              const net::IPEndPoint& address,
              net::CompletionOnceCallback callback) override;
  bool IsConnected() override;
  bool GetPeerAddress(net::IPEndPoint* address) override;
  bool GetLocalAddress(net::IPEndPoint* address) override;
  Socket::SocketType GetSocketType() const override;

  // Joins a multicast group. Can only be called after a successful Bind().
  void JoinGroup(const std::string& address,
                 net::CompletionOnceCallback callback);
  // Leaves a multicast group. Can only be called after a successful Bind().
  void LeaveGroup(const std::string& address,
                  net::CompletionOnceCallback callback);

  // Multicast options must be set before Bind()/Connect() is called.
  int SetMulticastTimeToLive(int ttl);
  int SetMulticastLoopbackMode(bool loopback);

  // Sets broadcast to |enabled|. Can only be called after a successful Bind().
  void SetBroadcast(bool enabled, net::CompletionOnceCallback callback);

  const std::vector<std::string>& GetJoinedGroups() const;

 protected:
  int WriteImpl(net::IOBuffer* io_buffer,
                int io_buffer_size,
                const net::CompletionCallback& callback) override;

 private:
  // Make net::IPEndPoint can be refcounted
  typedef base::RefCountedData<net::IPEndPoint> IPEndPoint;

  bool IsConnectedOrBound() const;

  // network::mojom::UDPSocketReceiver implementation.
  void OnReceived(int32_t result,
                  const base::Optional<net::IPEndPoint>& src_addr,
                  base::Optional<base::span<const uint8_t>> data) override;

  void OnConnectCompleted(net::CompletionOnceCallback user_callback,
                          const net::IPEndPoint& remote_addr,
                          int result,
                          const base::Optional<net::IPEndPoint>& local_addr);
  void OnBindCompleted(net::CompletionOnceCallback user_callback,
                       int result,
                       const base::Optional<net::IPEndPoint>& local_addr);
  void OnSendToCompleted(net::CompletionOnceCallback user_callback,
                         size_t byte_count,
                         int result);
  void OnWriteCompleted(const net::CompletionCallback& user_callback,
                        size_t byte_count,
                        int result);
  void OnJoinGroupCompleted(net::CompletionOnceCallback user_callback,
                            const std::string& normalized_address,
                            int result);
  void OnLeaveGroupCompleted(net::CompletionOnceCallback user_callback,
                             const std::string& normalized_address,
                             int result);

  network::mojom::UDPSocketPtr socket_;
  network::mojom::UDPSocketOptionsPtr socket_options_;

  bool is_bound_;
  mojo::Binding<network::mojom::UDPSocketReceiver> receiver_binding_;
  base::Optional<net::IPEndPoint> local_addr_;
  base::Optional<net::IPEndPoint> peer_addr_;

  ReadCompletionCallback read_callback_;

  RecvFromCompletionCallback recv_from_callback_;

  std::vector<std::string> multicast_groups_;
};

// UDP Socket instances from the "sockets.udp" namespace. These are regular
// socket objects with additional properties related to the behavior defined in
// the "sockets.udp" namespace.
class ResumableUDPSocket : public UDPSocket {
 public:
  ResumableUDPSocket(network::mojom::UDPSocketPtrInfo socket,
                     network::mojom::UDPSocketReceiverRequest receiver_request,
                     const std::string& owner_extension_id);

  // Overriden from ApiResource
  bool IsPersistent() const override;

  const std::string& name() const { return name_; }
  void set_name(const std::string& name) { name_ = name; }

  bool persistent() const { return persistent_; }
  void set_persistent(bool persistent) { persistent_ = persistent; }

  int buffer_size() const { return buffer_size_; }
  void set_buffer_size(int buffer_size) { buffer_size_ = buffer_size; }

  bool paused() const { return paused_; }
  void set_paused(bool paused) { paused_ = paused; }

 private:
  friend class ApiResourceManager<ResumableUDPSocket>;
  static const char* service_name() { return "ResumableUDPSocketManager"; }

  // Application-defined string - see sockets_udp.idl.
  std::string name_;
  // Flag indicating whether the socket is left open when the application is
  // suspended - see sockets_udp.idl.
  bool persistent_;
  // The size of the buffer used to receive data - see sockets_udp.idl.
  int buffer_size_;
  // Flag indicating whether a connected socket blocks its peer from sending
  // more data - see sockets_udp.idl.
  bool paused_;
};

}  //  namespace extensions

#endif  // EXTENSIONS_BROWSER_API_SOCKET_UDP_SOCKET_H_
