#include "CommandHandler.h"

CommandHandler::CommandHandler(const char *delim): delimiter(delim), defaultHandler(nullptr) {
    commands = nullptr;
}

CommandHandler::~CommandHandler() {
    while (commands) {
        Command *previous = commands;
        commands = commands->next;
        delete previous;
    }
}

void CommandHandler::registerCommand(const char *cmd, const char *desc, cmd_handler_t handler) {
    commands = new Command{cmd, desc, handler, commands};
}

void CommandHandler::setDefaultHandler(cmd_handler_t handler) {
    this->defaultHandler = handler;
}

void CommandHandler::parseCommand(const Sender &sender, String line) {
    line.trim();
    char *command = (char*) malloc((line.length() + 1) * sizeof(char));
    strcpy(command, line.c_str());
    int argc = 0;
    char *argv[11];
    argv[argc] = strtok(command, delimiter);
    while (argv[argc] != NULL) {
        ++argc;
        argv[argc] = strtok(NULL, delimiter);
    }
    handleCommand(sender, argc, argv);
    free(command);
}

void CommandHandler::handleCommand(const Sender &sender, int argc, char *argv[]) {
    Command *command = commands;
    while (command) {
        if (strcmp(argv[0], command->cmd) == 0) {
            command->handler(sender, argc - 1, argv + 1);
            return;
        }
        command = command->next;
    }
    if (defaultHandler) {
        defaultHandler(sender, argc, argv);
    }
}

void CommandHandler::printHelp(const Sender &sender) {
    sender.send("----- Command helps -----");
    Command *command = commands;
    while (command) {
        String str(command->cmd);
        str += " - ";
        str += command->description;
        sender.send(str.c_str());
        command = command->next;
    }
}
