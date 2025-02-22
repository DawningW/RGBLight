/**
 * 简易命令解析器, 此文件以 MIT 协议开源
 * 
 * @author QingChenW
 * @copyright MIT license
 */

#ifndef __COMMANDHANDLER_HPP__
#define __COMMANDHANDLER_HPP__

#include <functional>
#include <Arduino.h>
#include <WString.h>

#define MAX_ARG_COUNT 10

typedef std::function<void(const char *msg)> SenderFunc;
typedef std::function<void(SenderFunc sender, int argc, char *argv[])> HandlerFunc;

struct Command {
    Command *next;
    const char *name;
    const char *description;
    HandlerFunc handler;
};

class CommandHandler {
private:
    const char *delimiter;
    HandlerFunc defaultHandler;
    Command *commands;
    
public:
    CommandHandler(const char *delim = ","):
        delimiter(delim), defaultHandler(nullptr), commands(nullptr) {}

    ~CommandHandler() {
        while (commands) {
            Command *prev = commands;
            commands = commands->next;
            delete prev;
        }
    }

    void registerCommand(const char *name, const char *desc, HandlerFunc handler) {
        commands = new Command{commands, name, desc, handler};
    }

    void setDefaultHandler(HandlerFunc handler)  {
        defaultHandler = handler;
    }

    void parseCommand(SenderFunc sender, String line) {
        line.trim();
        if (line.length() == 0) {
            return;
        }
        char *command = line.begin();
        int argc = 0;
        char *argv[1 + MAX_ARG_COUNT];
        argv[argc] = strtok(command, delimiter);
        while (argv[argc] != NULL) {
            argv[++argc] = strtok(NULL, delimiter);
            if (argc >= MAX_ARG_COUNT) {
                break;
            }
        }
        handleCommand(sender, argc, argv);
    }

    void handleCommand(SenderFunc sender, int argc, char *argv[]) {
        Command *command = commands;
        while (command) {
            if (strcmp(argv[0], command->name) == 0) {
                command->handler(sender, argc, argv);
                return;
            }
            command = command->next;
        }
        if (defaultHandler) {
            defaultHandler(sender, argc, argv);
        }
    }

    void printHelp(SenderFunc sender) {
        sender("----- Command helps -----");
        Command *command = commands;
        while (command) {
            String str(command->name);
            str += " - ";
            str += command->description;
            sender(str.c_str());
            command = command->next;
        }
    }
};

#if !defined(NO_GLOBAL_INSTANCES) && !defined(NO_GLOBAL_CMDHANDLER)
CommandHandler cmdHandler;
#endif

#endif // __COMMANDHANDLER_HPP__
