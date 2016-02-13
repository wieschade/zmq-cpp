#include <iostream>
#include <chrono>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>

#include <cassert>

#include <zmq.h>

namespace zmq {


//Класс-обёртка над контекстом ØMQ
class Context {
  public:
    // Конструктор по умолчанию.
    // Вызывает zmq_ctx_new
    Context() {
      context_ = zmq_ctx_new();
      assert(context_);
    }
    ~Context() { zmq_ctx_term(context_); }
    // Получает опцию контекста.
    // Вызывает zmq_ctx_get.
    int get(int option_name) const {
      return zmq_ctx_get(context_, option_name);
    }
    // Устанавливает опцию контекста.
    // Вызывает zmq_ctx_set
    int set(int option_name, int option_value) const {
      return zmq_ctx_set(context_, option_name, option_value);
    }
    // Возвращает контекст ØMQ.
    // Внимание! Использовать только для низкоуровневых операций.
    operator void*() { return context_; }
  private:
    void* context_ = nullptr;
};

class Socket {
  public:
    // Низкоуровневый конструктор.
    // Вызывает zmq_socket.  
    Socket(void* context, int type) {
      socket_ = zmq_socket(context, type);
      assert(socket_);
    }
    Socket(Context& context, int type) :
      Socket((void*)context, type) {}
    // Сокет нельзя копировать
    Socket(const Socket& other) = delete;
    // Сокет нельзя копитровать присваиванием
    Socket& operator =(const Socket& other) = delete;
    ~Socket() { zmq_close(socket_); }
    // Вызывает zmq_bind
    int Bind(const char* endpoint) const {
      int rc = zmq_bind(socket_, endpoint);
      assert(rc != -1);
      return rc;
    }
    int Bind(std::string&& endpoint) const {
      return Bind(endpoint.c_str());
    }
    int Connect(const char* endpoint) const {
      int rc = zmq_connect(socket_, endpoint);
      assert(rc != -1);
      return rc;
    }
    int Connect(std::string&& endpoint) const {
      return Connect(endpoint.c_str());
    }
    //TODO: Переработать указатели
    int Send(void* buf, size_t len, int flags = 0) {
      int rc = zmq_send(socket_, buf, len, flags);
      return rc;
    }
    //TODO: Переработать указатели
    int Recv(void* buf, size_t len, int flags = 0) {
      int rc = zmq_recv(socket_, buf, len, flags);
      return rc;
    }
    //TODO: Переработать option_value
    int getsockopt(int option_name
                  ,const void* option_value
                  ,size_t option_len) const {
      return zmq_setsockopt(socket_, option_name, option_value, option_len);
    }
    //TODO: Переработать option_value
    int setsockopt(int option_name
                  ,const void* option_value
                  ,size_t option_len) const {
      return zmq_setsockopt(socket_, option_name, option_value, option_len);
    }
  operator void*() { return socket_; }  
  private:
    void* socket_ = nullptr;
};

class Message {
  public:
    Message() {
      int rc = zmq_msg_init(&message_);
      assert(rc == 0);
    }
    //TODO: Переработать указатель
    Message(void* data, size_t size) {
      int rc = zmq_msg_init_data(&message_, data, size, nullptr, nullptr);
      assert(rc == 0);
    }
    explicit Message(size_t size) {
      int rc = zmq_msg_init_size(&message_, size);
      assert(rc == 0);
    }
    ~Message() {
      zmq_msg_close(&message_);
    }
    operator zmq_msg_t*() { return &message_; }
    //TODO: Сделать метод const
    void* data() {
      void* data = zmq_msg_data(&message_);
      assert(data);
      return data;
    }
    //TODO: Переработать указатели
    //TODO: Сделать метод const
    int Recv(void* socket, int flags = 0) {
      int rc = zmq_msg_recv(&message_, socket, flags);
      return rc;
    }
    //TODO: Переработать указатели
    //TODO: Сделать метод const
    int Send(void* socket, int flags = 0) {
      int rc = zmq_msg_send(&message_, socket, flags);
      return rc;
    }
  private:
    //TODO: unique_ptr
    zmq_msg_t message_;
};

} //namespace zmq

class Rep {
  public:
    explicit Rep(zmq::Context& context) : socket_{context, ZMQ_REP} {}
    void Bind(std::string&& endpoint) const {
      socket_.Bind(std::move(endpoint));
    }
    void Run() {
      thread_ = std::thread{&Rep::Loop, this};
    }
    void Join() { thread_.join(); }
  private:
    void Loop() {
      for (;;) {
        zmq::Message message{};
        message.Recv(socket_);
        std::cout << (char*)message.data() << std::endl;
        socket_.Send((void*)"OK", 3);
      }
    }
    zmq::Socket socket_;
    std::thread thread_;
};

class Req {
  public:
    explicit Req(zmq::Context& context) : socket_{context, ZMQ_REQ} {}
    void Connect(std::string&& endpoint) {
      socket_.Connect(std::move(endpoint));
    }
    void Run() {
      thread_ = std::thread{&Req::Loop, this};
    }
    void Join() { thread_.join(); }
  private:
    void Loop() {
      std::string s;
      for (;;) {
        std::cin >> s;
        zmq::Message message{(void*)s.c_str(), s.size()+1};
        message.Send(socket_);
        zmq::Message msg;
        msg.Recv(socket_);
        //std::cout << (char*)msg.data() << std::endl;
        assert(std::string((char*)msg.data()) == "OK");
      }
    }
    zmq::Socket socket_;
    std::thread thread_;
};

static auto zmq_ctx = zmq::Context{};

int server() {
  Rep rep{zmq_ctx};
  rep.Bind("tcp://127.0.0.1:2222");
  rep.Run();

  Req req{zmq_ctx};
  req.Connect("tcp://127.0.0.1:2223");
  req.Run();

  rep.Join();
  req.Join();

  return 0;
}

int client() {
  Req req{zmq_ctx};
  req.Connect("tcp://127.0.0.1:2222");
  req.Run();

  Rep rep{zmq_ctx};
  rep.Bind("tcp://127.0.0.1:2223");
  rep.Run();
  
  req.Join();
  rep.Join();

  return 0;
}

int main(int argc, char *argv[]) {
  if (argc < 2) {
    std::cerr << "Invalid number of args" << std::endl;
    return 2;
  }
  
  if (std::string(argv[1]) == "server") {
    std::cout << "server" << std::endl;
    return server();
  } else if (std::string(argv[1]) == "client") {
    std::cout << "client" << std::endl;
    return client();
  }
    
}
