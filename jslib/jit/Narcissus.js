
(function() {

    var narcissus = {
        options: {
            version: 185,
        },
        hostGlobal: this
    };
    Narcissus = narcissus;
})();

Narcissus.definitions = require('./narcissus/jsdefs').definitions;
Narcissus.lexer = require('./narcissus/jslex').lexer;
Narcissus.parser = require('./narcissus/jsparse').parser;
Narcissus.decompiler = require('./narcissus/jsdecomp').decompiler;

exports.Narcissus = Narcissus;
