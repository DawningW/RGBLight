/*
 * 工具函数
 */

/**
 * polyfill for String.prototype.padStart
 * 
 * @param {String} str the string to pad
 * @param {Number} width pad the string at start with '0' to the given width
 * @returns {String} the result string
 */
function zfill(str, width) {
    str = String(str);
    while (str.length < width) {
        str = "0" + str;
    }
    return str;
}

/**
 * convert rgb color to hex color
 * 
 * @param {Number} r r component of color
 * @param {Number} g g component of color
 * @param {Number} b b component of color
 * @returns {String} hex color string
 */
function rgb2hex(r, g, b) {
    return "#" + zfill((r << 16 | g << 8 | b).toString(16), 6);
}

/**
 * convert hex color to rgba color
 * 
 * @param {String} color hex color string
 * @returns {{ r: Number, g: Number, b: Number, a: Number }} rgba color object
 */
function hex2rgba(color) {
    if (color.at(0) === "#" && (color.length === 7 || color.length === 9)) {
        return {
            r: parseInt(color.slice(1, 3), 16) / 255,
            g: parseInt(color.slice(3, 5), 16) / 255,
            b: parseInt(color.slice(5, 7), 16) / 255,
            a: color.length === 9 ? parseInt(color.slice(7, 9), 16) / 255 : 1,
        };
    }
    throw new Error("invalid hex color string");
}

/**
 * polyfill for TextDecoder
 * 
 * @param {Uint8Array} bytes utf-8 encoded bytes
 * @returns {String} decoded string
 */
function bytes2str(bytes) {
    return String.fromCodePoint(...Array.from(bytes));
}

export { zfill, rgb2hex, hex2rgba, bytes2str };
