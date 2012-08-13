
//
// Context
//

var DebugWebCL = false;

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

WebCLContext.prototype.mapData = function(arrayBufferView) {
  DEBUG("mapData: source data =", arrayBufferView, "byteLength=", arrayBufferView.byteLength);
  var clBuffer = this.ctx.createBuffer(WebCL.CL_MEM_READ_ONLY, arrayBufferView.byteLength);
  this.q.enqueueWriteBuffer(clBuffer, true, 0, arrayBufferView.byteLength, arrayBufferView, []);
  return new WebCLBufferWrapper(this, clBuffer, arrayBufferView);
};

WebCLContext.prototype.allocateData = function(elemType, numElems) {
  var elemSize = elemType.byteLength / elemType.length;
  var allocSize = numElems * (elemSize < 4 ? 4 : elemSize);
  DEBUG("allocateData: elemType =", elemType, "byteLength=", allocSize);
  var clBuffer = this.ctx.createBuffer(WebCL.CL_MEM_READ_WRITE, allocSize);
  var arrayBufferView = elemType.constructor(numElems);
  return new WebCLBufferWrapper(this, clBuffer, arrayBufferView);
};

WebCLContext.prototype.allocateData2 = function(data, length) {
  throw 'allocateData2 not implemented';
};

WebCLContext.prototype.compileKernel = function(source, name, options) {
  DEBUG("compileKernel:", source);
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

// Here's an example kernel that computes the Mandelbrot set. The
// kernel has just one actual argument: RT1_scale.  The others
// (_FAILRET, retVal, and retVal__offset) are inserted by RiverTrail
// for passing error codes and return values.
/*
__kernel void RT_computeSet(__global int *_FAILRET,
                            double RTl_scale,
                            __global double* retVal,
                            int retVal__offset) 
{
  int _sel_idx_tmp;
  int _FAIL = 0;
  int _id_0 = (int)get_global_id(0);
  int _id_1 = (int)get_global_id(1);
  int _writeoffset = 0+1 * _id_1+512 * _id_0;
  _writeoffset += retVal__offset;
  double  tempResult;
  __private const int RTl_iv[2] =  { _id_0, _id_1};

  {  
    int RTl_x;
    int RTl_y;
    double RTl_Cr;
    double RTl_Ci;
    double RTl_I;
    double RTl_R;
    double RTl_I2;
    double RTl_R2;
    int RTl_n;
    RTl_x = RTl_iv[1];
    RTl_y = RTl_iv[0];
    RTl_Cr = ((((double)((RTl_x- 256)))/RTl_scale)+ 0.407476);
    RTl_Ci = ((((double)((RTl_y- 256)))/RTl_scale)+ 0.234204);
    RTl_I =  0.0, RTl_R =  0.0, RTl_I2 =  0.0, RTl_R2 =  0.0;
    RTl_n =  0;
    while ( (((RTl_R2+RTl_I2)<((double) 2)) && (RTl_n< 512))) {
      RTl_I = ((((RTl_R+RTl_R))*RTl_I)+RTl_Ci);
      RTl_R = ((RTl_R2-RTl_I2)+RTl_Cr);
      RTl_R2 = (RTl_R*RTl_R);
      RTl_I2 = (RTl_I*RTl_I);
      RTl_n++;
    }
    tempResult = ((double)RTl_n);
    retVal[_writeoffset] =  tempResult;
    if (_FAIL) *_FAILRET = _FAIL;
    return;
  }
}
*/

function WebCLKernel(context, program, kernel)
{
  this.ctx = context;
  this.program = program;
  this.kernel = kernel;
  this.dummyArrayBuffer = new Int32Array(1);  // HACK HACK
  this.FAILRET = this.ctx.allocateData(this.dummyArrayBuffer, 16); // HACK HACK
}

WebCLKernel.prototype.setArgument = function(index, webCLBufferWrapper) {
  //DEBUG("kernel.setArgument: index =", index, "arg =", webCLBufferWrapper);
  this.kernel.setKernelArg(index+1, webCLBufferWrapper.clBuffer); // HACK HACK
};

WebCLKernel.prototype.setScalarArgument = function(index, arg, isInteger, highPrecision) {
  //DEBUG("kernel.setScalarArgument: index =", index, "arg =", arg);
  var type = isInteger ? WebCL.types.INT :
    (highPrecision ? WebCL.types.DOUBLE : WebCL.types.FLOAT);
  this.kernel.setKernelArg(index+1, arg, type);  // HACK HACK
};

WebCLKernel.prototype.run = function(rank, shape, tile) {
  try {
    var fname = this.kernel.getKernelInfo(WebCL.CL_KERNEL_FUNCTION_NAME);
    var prog = this.kernel.getKernelInfo(WebCL.CL_KERNEL_PROGRAM);
    DEBUG("kernel.run(" + fname + ")");

    this.kernel.setKernelArg(0, this.FAILRET.clBuffer);  // HACK HACK

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

function WebCLBufferWrapper(clContext, clBuffer, arrayBufferView)
{
  this.ctx = clContext;
  this.clBuffer = clBuffer;
  this.arrayBufferView = arrayBufferView;
}

WebCLBufferWrapper.prototype.getValue = function() {
  this.ctx.q.enqueueReadBuffer(this.clBuffer, true, 0, this.arrayBufferView.byteLength, this.arrayBufferView, []);
  this.clBuffer.releaseCLResources();
  this.clBuffer = undefined;
  DEBUG("WebCLBufferWrapper.getValue returned " + this.arrayBufferView.byteLength + " bytes", this.arrayBufferView);
  return this.arrayBufferView;
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
