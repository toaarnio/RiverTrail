
//
// Context
//

function WebCLContext(context)
{
    this.ctx = context;
    this.devices = context.getContextInfo(WebCL.CL_CONTEXT_DEVICES);
    this.buildLog = "";
    this.q = this.ctx.createCommandQueue(this.devices[0], 0);
}

WebCLContext.prototype.mapData = function(data) {
    var memobj = this.ctx.createBuffer(WebCL.CL_MEM_READ_ONLY, data.byteLength);
    this.q.enqueueWriteBuffer(memobj, true, 0, data.byteLength, data, []);
    return new WebCLMemoryObject(this, memobj, data);
};

WebCLContext.prototype.allocateData = function(data, length) {
    var elem_size = data.byteLength / data.length;
    var memobj = this.ctx.createBuffer(WebCL.CL_MEM_READ_WRITE, length * elem_size);
    return new WebCLMemoryObject(this, memobj, data);
};

WebCLContext.prototype.allocateData2 = function(data, length) {
    throw 'allocateData2 not implemented';
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
    return new WebCLKernel(this, kernel);
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
                                              WebCL.CL_DEVICE_TYPE_GPU);
    return new WebCLContext(context);
};

//
// Kernel
// 

function WebCLKernel(context, kernel)
{
    this.ctx = context;
    this.kernel = kernel;
}

WebCLKernel.prototype.setArgument = function(index, memobj) {
    this.kernel.setKernelArg(index, memobj.memobj, WebCL.types.MEM);
};

WebCLKernel.prototype.setScalarArgument = function(index, arg, isInteger, highPrecision) {
    var type = isInteger ? WebCL.types.INT :
        (highPrecision ? WebCL.types.DOUBLE : WebCL.types.FLOAT);
    this.kernel.setKernelArg(index, arg, type);
};

WebCLKernel.prototype.run = function(rank, shape, tile) {
    this.ctx.q.enqueueNDRangeKernel(this.kernel, rank, [], shape, [], []);
    this.ctx.q.finish();
};

//
// MemoryObject
//

function WebCLMemoryObject(context, memory_object, data)
{
    this.ctx = context;
    this.memobj = memory_object;
    this.data = data;
}

WebCLMemoryObject.prototype.getValue = function() {
    this.ctx.q.enqueueReadBuffer(this.memobj, true, 0, this.data.byteLength, this.data, []);
    this.memobj.releaseCLResources();
    this.memobj = undefined;
    return this.data;
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
