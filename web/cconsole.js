/**
 * Command console
 */
class CConsole {
    lineCount = 0;

    constructor(options) {
        this.messageText = options.messageText || document.getElementById("message");
        this.commandInput = options.commandInput || document.getElementById("command");
        this.sendButton = options.sendButton || document.getElementById("send");
        this.execute = options.executeCallback || ((command) => console.debug("No execute function defined"));
        const that = this;
        this.sendButton.addEventListener("click", (e) => {
            let command = that.commandInput.value;
            if (command && command.trim().length > 0) {
                that.commandInput.value = "";
                that.execute(command);
            }
        });
        this.commandInput.addEventListener("keydown", (e) => {
            let theEvent = e || window.event; // 兼容 IE 浏览器
            let code = theEvent.keyCode || theEvent.which || theEvent.charCode;
            if (code == 13) {
                that.sendButton.click();
            }
        });
    }

    print(message) {
        if (++this.lineCount > 100) {
            this.messageText.innerHTML = "";
            this.lineCount = 0;
        }
        this.messageText.innerHTML += message + "\n";
        this.messageText.scrollTop = this.messageText.scrollHeight;
    }
};

export default CConsole;
