
var WebCL = require('webcl');

//
// Context
//

function WebCLContext(context)
{
    this.ctx = context;
    this.devices = context.getContextInfo(WebCL.CL_CONTEXT_DEVICES);
    this.buildLog = "";
}

var makeGetValue = function(dev, ctx, memobj, data) {
    return function() {
        var q = ctx.createCommandQueue(dev, 0);
        q.enqueueReadBuffer(memobj, true, 0, data.byteLength, data, []);
        return data;
    };
};

WebCLContext.prototype.mapData = function(data) {
    var memobj = this.ctx.createBuffer(WebCL.CL_MEM_READ_ONLY, data.byteLength);
    var dev = this.devices[0];
    var q = this.ctx.createCommandQueue(dev, 0);
    q.enqueueWriteBuffer(memobj, true, 0, data.byteLength, data, []);
    memobj.getValue = makeGetValue(dev, this.ctx, memobj, data);
    return memobj;
};

WebCLContext.prototype.allocateData = function(data, length) {
    var elem_size = data.byteLength / data.length;
    var memobj = this.ctx.createBuffer(WebCL.CL_MEM_READ_WRITE, length * elem_size);
    var dev = this.devices[0];
    memobj.getValue = makeGetValue(dev, this.ctx, memobj, data);
    return memobj;
};

WebCLContext.prototype.compileKernel = function(source, name, options) {
    options = options || "";
    this.buildLog = "";
    var dev = this.devices[0];
    var program = this.ctx.createProgramWithSource(source);
    try {
        program.buildProgram([dev], options);
    } catch(e) {
        var err = program.getProgramBuildInfo(dev, WebCL.CL_PROGRAM_BUILD_STATUS);
        this.buildLog = program.getProgramBuildInfo(dev, WebCL.CL_PROGRAM_BUILD_LOG);
        throw err;
    }
    var kernel = program.createKernel(name);
    return new WebCLKernel(this.ctx, dev, kernel);
};

//
// Platform
//

function WebCLPlatform(platform)
{
    this.platform = platform;
}

WebCLPlatform.prototype.createContext = function() {
    var context = WebCL.createContextFromType([WebCL.CL_CONTEXT_PLATFORM,
                                               this.platform],
                                              WebCL.CL_DEVICE_TYPE_DEFAULT);
    return new WebCLContext(context);
};

//
// Kernel
// 

function WebCLKernel(context, device, kernel)
{
    this.kernel = kernel;
    this.dev = device;
    this.ctx = context;
}

WebCLKernel.prototype.setArgument = function(index, memobj) {
    this.kernel.setKernelArg(index, memobj, WebCL.types.MEM);
};

WebCLKernel.prototype.setScalarArgument = function(index, arg, isInteger, highPrecision) {
    var type = isInteger ? WebCL.types.INT :
        (highPrecision ? WebCL.types.DOUBLE : WebCL.types.FLOAT);
    this.kernel.setKernelArg(index, arg, type);
};

WebCLKernel.prototype.run = function(rank, shape, tile) {
    var q = this.ctx.createCommandQueue(this.dev, 0);
    q.enqueueNDRangeKernel(this.kernel, rank, [], shape, [], []);
    q.finish();
};

//
// DPOInterface
//

function WebCLWrapper()
{
}

WebCLWrapper.prototype.getPlatform = function() {
    var platforms = WebCL.getPlatformIDs();
    return new WebCLPlatform(platforms[0]);
};


exports.DPOInterface = WebCLWrapper;