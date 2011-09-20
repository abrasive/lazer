#include <OpenGL/gl.h>
#include <OpenGL/CGLTypes.h>
#include <GLUT/glut.h>
#include <stdio.h>
#include <math.h>

#include <AudioUnit/AudioUnit.h>
#include <CoreAudio/CoreAudio.h>
#include <CoreAudio/AudioHardware.h>

#define RING_SIZE (0x40000)
short ringbuf[RING_SIZE*4];
volatile int wptr = 0, rptr = 0;

int fullbright = 0;

void mouse (int button, int state, int x, int y) {
    if (button == GLUT_LEFT_BUTTON && state == GLUT_DOWN) {
        fullbright = !fullbright;
        glutSetWindowTitle(fullbright ? "glLaserCanvas - FULLBRIGHT" : "glLaserCanvas");
    }
}
    
AudioComponentInstance auHAL;

//#include <AudioToolbox/AudioQueue.h>

AudioBufferList *theBufferList;

OSStatus callback(
        void *inRefCon,
        AudioUnitRenderActionFlags *ioActionFlags,
        const AudioTimeStamp *inTimeStamp,
        UInt32 inBusNumber,
        UInt32 inNumberFrames,
        AudioBufferList * ioData) {
    int err = AudioUnitRender(auHAL,
            ioActionFlags,
            inTimeStamp, 
            inBusNumber,     //will be '1' for input data
            inNumberFrames, //# of frames requested
            theBufferList);
    SInt16 *data = theBufferList->mBuffers[0].mData;
    theBufferList->mBuffers[0].mData = NULL;
    int to_write = inNumberFrames;
    int write_now;
    if (wptr + to_write >= RING_SIZE) {
        //fprintf(stderr, "wrapping - wptr %d, tw %d, rs %d\n", wptr, to_write, RING_SIZE);
        fprintf(stderr, "w");
        write_now = RING_SIZE - wptr - 1;
        memcpy(ringbuf + 4*wptr, data, 4*2*write_now);
        to_write -= write_now;
        data += write_now*4;
        wptr = 0;
    }
    memcpy(ringbuf + 4*wptr, data, 4*2*to_write);
    wptr += to_write;
}
    
#define n_buffers (1)

int record_main(void) {
    AudioBuffer *buf;
    theBufferList = malloc(sizeof(AudioBufferList) + sizeof(AudioBuffer*)*n_buffers);
    theBufferList->mNumberBuffers = n_buffers;
    int i;
    for (i=0; i<n_buffers; i++) {
        buf = theBufferList->mBuffers + i;
        buf->mNumberChannels = 3;
        buf->mDataByteSize = 3*1000;
        buf->mData = 0; // malloc(3*1000);
    }

    AudioComponent comp;
    AudioComponentDescription desc;

    //There are several different types of Audio Units.
    //Some audio units serve as Outputs, Mixers, or DSP
    //units. See AUComponent.h for listing
    desc.componentType = kAudioUnitType_Output;

    //Every Component has a subType, which will give a clearer picture
    //of what this components function will be.
    desc.componentSubType = kAudioUnitSubType_HALOutput;

     //all Audio Units in AUComponent.h must use 
     //"kAudioUnitManufacturer_Apple" as the Manufacturer
    desc.componentManufacturer = kAudioUnitManufacturer_Apple;
    desc.componentFlags = 0;
    desc.componentFlagsMask = 0;

    //Finds a component that meets the desc spec's
    comp = AudioComponentFindNext(NULL, &desc);
    if (comp == NULL) exit (-1);

     //gains access to the services provided by the component
    AudioComponentInstanceNew(comp, &auHAL);

 UInt32 enableIO;
     UInt32 size=0;

     //When using AudioUnitSetProperty the 4th parameter in the method
     //refer to an AudioUnitElement. When using an AudioOutputUnit
     //the input element will be '1' and the output element will be '0'.


      enableIO = 1;
      AudioUnitSetProperty(auHAL,
                kAudioOutputUnitProperty_EnableIO,
                kAudioUnitScope_Input,
                1, // input element
                &enableIO,
                sizeof(enableIO));

      enableIO = 0;
      AudioUnitSetProperty(auHAL,
                kAudioOutputUnitProperty_EnableIO,
                kAudioUnitScope_Output,
                0,   //output element
                &enableIO,
                sizeof(enableIO));

    OSStatus err =noErr;
    size = sizeof(AudioDeviceID);

    AudioDeviceID inputDevice;
    err = AudioHardwareGetProperty(kAudioHardwarePropertyDefaultInputDevice,
                                                  &size, 
                                                  &inputDevice);

    if (err)
        exit(err);

    err =AudioUnitSetProperty(auHAL,
                         kAudioOutputUnitProperty_CurrentDevice, 
                         kAudioUnitScope_Global, 
                         0, 
                         &inputDevice, 
                         sizeof(inputDevice));
    if (err)
        exit(err);

    AudioStreamBasicDescription DeviceFormat;
    AudioStreamBasicDescription DesiredFormat;
   //Use CAStreamBasicDescriptions instead of 'naked' 
   //AudioStreamBasicDescriptions to minimize errors.
   //CAStreamBasicDescription.h can be found in the CoreAudio SDK.

    size = sizeof(AudioStreamBasicDescription);

     //Get the input device format
    AudioUnitGetProperty (auHAL,
                                   kAudioUnitProperty_StreamFormat,
                                   kAudioUnitScope_Input,
                                   1,
                                   &DeviceFormat,
                                   &size);

    //set the desired format to the device's sample rate
    memcpy(&DesiredFormat, &DeviceFormat, sizeof(AudioStreamBasicDescription));

    DesiredFormat.mSampleRate =  DeviceFormat.mSampleRate;
    DesiredFormat.mChannelsPerFrame = 4;
    DesiredFormat.mBitsPerChannel = 16;
    DesiredFormat.mBytesPerPacket = DesiredFormat.mBytesPerFrame =
        DesiredFormat.mChannelsPerFrame * 2;
    DesiredFormat.mFramesPerPacket = 1;
    DesiredFormat.mFormatFlags = kLinearPCMFormatFlagIsSignedInteger | kLinearPCMFormatFlagIsPacked;

     //set format to output scope
    err = AudioUnitSetProperty(
                            auHAL,
                            kAudioUnitProperty_StreamFormat,
                            kAudioUnitScope_Output,
                            1,
                            &DesiredFormat,
                            sizeof(AudioStreamBasicDescription));
    if (err)
        exit(err);

    SInt32 *channelMap =NULL;
    UInt32 numOfChannels = DesiredFormat.mChannelsPerFrame; //2 channels
    UInt32 mapSize = numOfChannels *sizeof(SInt32);
    channelMap = (SInt32 *)malloc(mapSize);

    //for each channel of desired input, map the channel from
    //the device's output channel. 
    for(i=0;i<numOfChannels;i++)
    {
        channelMap[i]=i;
    }
    err = AudioUnitSetProperty(auHAL,
                                        kAudioOutputUnitProperty_ChannelMap,
                                        kAudioUnitScope_Output,
                                        1,
                                        channelMap,
                                        size);
    if (err)
        exit(err);
    free(channelMap);

    AURenderCallbackStruct input;
    input.inputProc = callback;
    input.inputProcRefCon = 0;

    err = AudioUnitSetProperty(
            auHAL, 
            kAudioOutputUnitProperty_SetInputCallback, 
            kAudioUnitScope_Global,
            0,
            &input, 
            sizeof(input));
    if (err)
        exit(err);

     err = AudioUnitInitialize(auHAL);
    if (err)
        exit(err);
     err = AudioOutputUnitStart(auHAL);
    if (err)
        exit(err);
}


double t = 0.0;
double last_x = 0, last_y = 0;

int times = 0;

void display(void) {
    if (rptr==wptr) {
        glutSwapBuffers();
        glutPostRedisplay();
        return;
    }

    glDrawBuffer(GL_BACK);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    int n;
    double x, y;
    glBegin(GL_LINES);
    glColor4f(0,1,0,0.7);
    glVertex2f(last_x, last_y);

    n = 0;
    //fprintf(stderr, "b %f %f %f %f\n", ringbuf[rptr*4+0]/32768.0, ringbuf[rptr*4+1]/32768.0, ringbuf[rptr*4+2]/32768.0, ringbuf[rptr*4+3]/32768.0);
again:
    while (rptr != wptr) {
        x = ringbuf[rptr*4] / 32768.0;
        y = ringbuf[rptr*4+1] / 32768.0;
        if (!fullbright)
            glColor4f(0, ringbuf[rptr*4+2]/32768.0, 0,0.7);
        rptr++;
        if (rptr>=RING_SIZE) {
            rptr = 0;
            fprintf(stderr, "r");
        }
        glVertex2f(x, y);
        glVertex2f(x, y);
        n++;
    }
    if (n < 1000) {
        glutSwapBuffers();
        goto again;
    }
    glVertex2f(x, y);
    last_x = x; last_y = y;
    glEnd();

#define SKIP_ACCUM
#ifndef SKIP_ACCUM
    glAccum(GL_MULT, 0.7);
    glAccum(GL_ACCUM, 1.0);

    glDrawBuffer(GL_FRONT);
    glAccum(GL_RETURN, 1.0);
    glFlush();
#else
    //glDrawBuffer(GL_BACK);
    glutSwapBuffers();
#endif

    glutPostRedisplay();
}

    CGLContextObj CGLGetCurrentContext(void);
int main(int argc, char **argv) {
    glutInit(&argc, argv);
    glutInitDisplayMode(GLUT_RGB | GLUT_DEPTH | GLUT_DOUBLE | GLUT_ACCUM);
    glutInitWindowSize(600,600);
    glutCreateWindow("glLaserCanvas");
    glutDisplayFunc(display);
    glutMouseFunc(mouse);

    long sync = 1;
    CGLContextObj ctx = CGLGetCurrentContext();
    CGLSetParameter (ctx, kCGLCPSwapInterval, &sync);  
    


    glLoadIdentity();
    glDrawBuffer(GL_FRONT);
    glClearColor(0,0,0,1);
    glClearAccum(0,0,0,1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_ACCUM_BUFFER_BIT);
    glDrawBuffer(GL_BACK);
    glReadBuffer(GL_BACK);
    glAccum(GL_RETURN, 1.0);
    //glutSwapBuffers();
    
    record_main();
    glutMainLoop();
    return 0;
}
