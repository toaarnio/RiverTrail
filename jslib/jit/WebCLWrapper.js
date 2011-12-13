
//
// Context
//

var DebugWebCL = true;

function DEBUG() {
  if (DebugWebCL === true) {
    if (arguments.length > 1) {
      console.log(arguments);
    } else {
      console.log(arguments[0]);
    }
  }
}

function WebCLContext(context)
{
  this.ctx = context;
  this.devices = context.getContextInfo(WebCL.CL_CONTEXT_DEVICES);
  this.buildLog = "";
  this.q = this.ctx.createCommandQueue(this.devices[0], 0);
}

WebCLContext.prototype.mapData = function(data) {
  DEBUG("mapData: data =", data, "byteLength=", data.byteLength);
  var memobj = this.ctx.createBuffer(WebCL.CL_MEM_READ_ONLY, data.byteLength);
  this.q.enqueueWriteBuffer(memobj, true, 0, data.byteLength, data, []);
  return new WebCLMemoryObject(this, memobj, data);
};

WebCLContext.prototype.allocateData = function(data, length) {
  var elem_size = data.byteLength / data.length;
  var allocSize = elem_size < 4 ? length * 4 : length * elem_size;
  DEBUG("allocateData: data =", data, "byteLength=", allocSize);
  var memobj = this.ctx.createBuffer(WebCL.CL_MEM_READ_WRITE, allocSize);
  return new WebCLMemoryObject(this, memobj, data);
};

WebCLContext.prototype.allocateData2 = function(data, length) {
  throw 'allocateData2 not implemented';
};

WebCLContext.prototype.compileKernel = function(source, name, options) {
  DEBUG("compileKernel:");
  DEBUG(source);
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
  return new WebCLKernel(this, program, kernel);
};

//
// Platform
//

function WebCLPlatform(platform)
{
  this.platform = platform;
}

//
// Try to create a CPU context; if that fails, try the GPU.  The CPU
// is preferred because the RiverTrail compiler doesn't seem to be
// producing very GPU-optimized kernels.
//
WebCLPlatform.prototype.createContext = function() {
  var context = null;
  try {
    context = WebCL.createContextFromType([WebCL.CL_CONTEXT_PLATFORM,
                                           this.platform],
                                          WebCL.CL_DEVICE_TYPE_CPU);
    console.log("Selected the CPU");
  } catch (e) {
    context = WebCL.createContextFromType([WebCL.CL_CONTEXT_PLATFORM,
                                           this.platform],
                                          WebCL.CL_DEVICE_TYPE_GPU);
    console.log("Selected the GPU");
  }
  return new WebCLContext(context);
};

//
// Kernel
// 

function WebCLKernel(context, program, kernel)
{
  this.ctx = context;
  this.program = program;
  this.kernel = kernel;
  this.dummyArrayBuffer = new Int32Array(64);  // HACK HACK
  this.FAILRET = this.ctx.allocateData(this.dummyArrayBuffer, 16); // HACK HACK
}

WebCLKernel.prototype.setArgument = function(index, memobj) {
  DEBUG("kernel.setArgument: index =", index, "arg =", memobj);
  this.kernel.setKernelArg(index+1, memobj.memobj); // HACK HACK
};

WebCLKernel.prototype.setScalarArgument = function(index, arg, isInteger, highPrecision) {
  DEBUG("kernel.setScalarArgument: index =", index, "arg =", arg);
  var type = isInteger ? WebCL.types.INT :
    (highPrecision ? WebCL.types.DOUBLE : WebCL.types.FLOAT);
  this.kernel.setKernelArg(index+1, arg, type);  // HACK HACK
};

WebCLKernel.prototype.run = function(rank, shape, tile) {
  try {
    var fname = this.kernel.getKernelInfo(WebCL.CL_KERNEL_FUNCTION_NAME);
    DEBUG("kernel.run: " + fname + "()");
    var prog = this.kernel.getKernelInfo(WebCL.CL_KERNEL_PROGRAM);
    var src = prog.getProgramInfo(WebCL.CL_PROGRAM_SOURCE);
    DEBUG("kernel source: ");
    DEBUG(src);

    this.kernel.setKernelArg(0, this.FAILRET.memobj);  // HACK HACK

    this.ctx.q.enqueueNDRangeKernel(this.kernel, rank, [], shape, [], []);
    this.ctx.q.finish();
  } catch (e) {
    console.log("Exception in kernel.run: ", e);
    return e;
  }
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
  DEBUG("WebCLMemoryObject.getValue: this.data.byteLength =", this.data.byteLength);
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

  // Since this demo originates from Intel, it presumably works the
  // best on the Intel OpenCL SDK. NVIDIA OpenCL drivers seem to be
  // the least robust, at least on Windows.

  var platformPreference = [ "Intel", "AMD", "ATI", "NVIDIA", "" ];

  var platforms = WebCL.getPlatforms();
  for (var i=0, found=false; i < platformPreference.length && !found; i++) {
    var pref = platformPreference[i];
    for (var p=0; p < platforms.length && !found; p++)  {
      var platform = platforms[p];
      var name = platforms[p].getPlatformInfo(WebCL.CL_PLATFORM_NAME);
      found = (name.indexOf(pref) != -1);
    }
  }
  console.log("Selected the following OpenCL Platform: ", name);
  return new WebCLPlatform(platform);
};
