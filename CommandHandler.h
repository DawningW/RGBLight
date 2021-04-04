#ifndef _COMMANDHANDLER_H_
#define _COMMANDHANDLER_H_
#include <Arduino.h>
#include <WString.h>

class Sender {
public:
    virtual void send(const char *msg) const = 0;
};

typedef void (*cmd_handler_t)(const Sender &sender, int argc, char *argv[]);

// TODO 用字典树实现
// https://github.com/xaxys/Oasis/blob/master/commandmanager.go
// TODO 命令别名

struct Command {
    const char *cmd;
    const char *description;
    cmd_handler_t handler;
    Command *next;
};

class CommandHandler {
private:
    const char *delimiter;
    cmd_handler_t defaultHandler;
    Command *commands;
    
public:
    CommandHandler(const char *delim = " ");
    ~CommandHandler();
    void registerCommand(const char *cmd, const char *desc, cmd_handler_t handler);
    void setDefaultHandler(cmd_handler_t handler);
    void parseCommand(const Sender &sender, String line);
    void handleCommand(const Sender &sender, int argc, char *argv[]);
    void printHelp(const Sender &sender);
};

#endif
