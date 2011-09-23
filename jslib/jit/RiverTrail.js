
global.RiverTrail = {};
exports.RiverTrail = {};

exports.RiverTrail.Helper = require('./compiler/helper').Helper;
exports.RiverTrail.definitions = require('./compiler/definitions').definitions;
exports.RiverTrail.compiler = require('./compiler/driver').compiler;
exports.RiverTrail.dotviz = require('./compiler/dotviz').dotviz;
exports.RiverTrail.Typeinference = require('./compiler/typeinference').Typeinference;
exports.RiverTrail.RangeAnalysis = require('./compiler/rangeanalysis').RangeAnalysis;
exports.RiverTrail.codeGen = require('./compiler/genOCL').codeGen;
exports.RiverTrail.runOCL = require('./compiler/runOCL').runOCL;

exports.RiverTrail = global.RiverTrail;

//global.RiverTrail = undefined;
