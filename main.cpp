#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/asio.hpp>
#include <boost/thread.hpp>
#include <iostream>
#include <string>
#include <array>
#include <cstdlib>
#include <memory>
#include <unistd.h>
#include <limits.h>  // Для получения текущего рабочего каталога
#include <fstream>   // Для работы с файлами
#include <netinet/in.h>
#include <ifaddrs.h>

namespace beast = boost::beast;        // откуда beast
namespace websocket = beast::websocket; // откуда websocket
namespace net = boost::asio;           // откуда net
using tcp = boost::asio::ip::tcp;      // откуда tcp

class session : public std::enable_shared_from_this<session> {
    websocket::stream<tcp::socket> ws_;
    beast::flat_buffer buffer_;
    std::string current_directory_;

public:
    explicit session(tcp::socket socket)
        : ws_(std::move(socket)), current_directory_(get_current_directory()) {}

    void run() {
        ws_.async_accept(
            beast::bind_front_handler(&session::on_accept, shared_from_this()));
    }

private:
    std::string get_current_directory() {
        char cwd[PATH_MAX];
        if (getcwd(cwd, sizeof(cwd)) != nullptr) {
            return std::string(cwd);
        } else {
            return "Failed to get current directory";
        }
    }

std::string execute_command(const std::string& command) {
    std::string zsh_command = "zsh -c \"" + command + "\"";

    if (command.substr(0, 3) == "cd ") {
        std::string new_directory = command.substr(3);
        if (chdir(new_directory.c_str()) == 0) {
            // Обновляем текущую директорию
            current_directory_ = get_current_directory();
            return ""; // Ничего не отправляем
        } else {
            return "Failed to change directory: " + new_directory;
        }
    }

    std::array<char, 128> buffer;
    std::string result;
    FILE* pipe = popen(zsh_command.c_str(), "r");
    if (!pipe) return "Failed to execute command";

    while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
        result += buffer.data();
    }
    pclose(pipe);
    return result;
}

    void on_accept(beast::error_code ec) {
        if (ec) {
            std::cerr << "Ошибка при принятии подключения: " << ec.message() << std::endl;
            return;
        }

        do_read();
    }

    void do_read() {
        auto self(shared_from_this());
        ws_.async_read(
            buffer_,
            [this, self](beast::error_code ec, std::size_t bytes_transferred) {
                if (ec) {
                    std::cerr << "Ошибка при чтении: " << ec.message() << std::endl;
                    return;
                }

                std::string command{beast::buffers_to_string(buffer_.data())};
                buffer_.consume(buffer_.size());

                // Убираем пробелы и выводим текущую директорию перед командой
                command = command.substr(0, command.find('\n')); // Убираем все после новой строки, если есть
                std::string result = execute_command(command);

                // Форматируем вывод
                std::string output;
                if (!result.empty()) {
                    output += result + "\n";
                }

                // Добавляем текущую директорию обратно клиенту
                output += current_directory_ + "> \n"; // Добавляем символ '>' после директории

                ws_.async_write(
                    boost::asio::buffer(output),
                    [this, self](beast::error_code ec, std::size_t /*bytes_transferred*/) {
                        if (ec) {
                            std::cerr << "Ошибка при отправке: " << ec.message() << std::endl;
                            return;
                        }
                        do_read();
                    });
            });
    }
};

class server {
    tcp::acceptor acceptor_;

public:
    server(net::io_context& ioc, tcp::endpoint endpoint)
        : acceptor_(ioc) {
        beast::error_code ec;

        acceptor_.open(endpoint.protocol(), ec);
        if (ec) throw beast::system_error{ec};

        acceptor_.bind(endpoint, ec);
        if (ec) throw beast::system_error{ec};

        acceptor_.listen(net::socket_base::max_listen_connections, ec);
        if (ec) throw beast::system_error{ec};

        do_accept();
    }

private:
    void do_accept() {
        acceptor_.async_accept(
            [this](beast::error_code ec, tcp::socket socket) {
                if (!ec) {
                    std::make_shared<session>(std::move(socket))->run();
                } else {
                    std::cerr << "Ошибка при принятии соединения: " << ec.message() << std::endl;
                }
                do_accept();
            });
    }
};

// Функция для получения IP-адреса
std::string get_ip_address() {
    struct ifaddrs *interfaces = nullptr;
    std::string ip_address;

    if (getifaddrs(&interfaces) == -1) return ip_address;

    for (struct ifaddrs *iface = interfaces; iface != nullptr; iface = iface->ifa_next) {
        if (iface->ifa_addr && iface->ifa_addr->sa_family == AF_INET) {
            char address_buffer[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &(((struct sockaddr_in *)iface->ifa_addr)->sin_addr), address_buffer, sizeof(address_buffer));
            ip_address = address_buffer; // Сохраняем первый найденный адрес
            break;
        }
    }
    freeifaddrs(interfaces);
    return ip_address;
}

void replace_ip_in_html(const std::string& file_path, const std::string& ip) {
    std::ifstream file(file_path);
    std::string content;
    std::string line;

    while (std::getline(file, line)) {
        // Замените "YOUR_IP" на ваш IP-адрес в файле
        size_t pos = line.find("YOUR_IP");
        if (pos != std::string::npos) {
            line.replace(pos, 8, ip);
        }
        content += line + "\n";
    }
    file.close();

    std::ofstream out_file(file_path);
    out_file << content;
    out_file.close();
}




int main() {
    try {
        net::io_context ioc;
        tcp::endpoint endpoint(tcp::v4(), 8080);
        server srv(ioc, endpoint);

        std::cout << "Server running  \n";

        // Запуск сервера Python в фоне
        std::string command = "python3 -m http.server 8081 &";
        std::system(command.c_str());

        // Открытие client.html
        command = "xdg-open http://192.168.1.66:8081/client.html"; // Убедитесь, что путь верный
        std::system(command.c_str());

        ioc.run();
    } catch (const std::exception& e) {
        std::cerr << "Ошибка: " << e.what() << "\n";
    }

    return 0;
}
