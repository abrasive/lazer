#include <OpenGL/gl.h>
#include <OpenGL/CGLTypes.h>
#include <GLUT/glut.h>
#include <math.h>

#define RING_SIZE (0x10000)
#define RING_MASK (0x0ffff)
short ringbuf[RING_SIZE*4];
volatile int wptr = 0, rptr = 0;

#include <AudioToolbox/AudioQueue.h>
#define n_buffers (8)
    
AudioStreamBasicDescription  mDataFormat;             
AudioQueueRef                mQueue;                   
AudioQueueBufferRef          mBuffers[n_buffers];  
UInt32                       bufferByteSize;              
SInt64                       mCurrentPacket;               
bool                         mIsRunning;                    

static void callback (void *param, AudioQueueRef queue, AudioQueueBufferRef buf, const AudioTimeStamp *start, uint32_t packets, const AudioStreamPacketDescription *desc) {
    if (!packets && mDataFormat.mBytesPerPacket)
        packets = buf->mAudioDataByteSize / mDataFormat.mBytesPerPacket;

    SInt16 *data = buf->mAudioData;
    int to_write = packets;
    int write_now;
    if (wptr + to_write >= RING_SIZE) {
        //fprintf(stderr, "wrapping - wptr %d, tw %d, rs %d\n", wptr, to_write, RING_SIZE);
        write_now = RING_SIZE - wptr - 1;
        memcpy(ringbuf + 4*wptr, data, 4*2*write_now);
        to_write -= write_now;
        data += write_now*4;
        wptr = 0;
    }
    memcpy(ringbuf + 4*wptr, data, 4*2*to_write);
    wptr += to_write;

    AudioQueueEnqueueBuffer(mQueue, buf, 0, NULL);
}


int record_main(void) {
    mDataFormat.mFormatID = kAudioFormatLinearPCM;
    mDataFormat.mSampleRate = 48000.0;
    mDataFormat.mChannelsPerFrame = 4;
    mDataFormat.mBitsPerChannel = 16;
    mDataFormat.mBytesPerPacket = mDataFormat.mBytesPerFrame =
        mDataFormat.mChannelsPerFrame * 2;
    mDataFormat.mFramesPerPacket = 1;
    mDataFormat.mFormatFlags = kLinearPCMFormatFlagIsSignedInteger | kLinearPCMFormatFlagIsPacked;

    AudioQueueNewInput(&mDataFormat,
            callback,
            NULL,
            NULL,
            kCFRunLoopCommonModes,
            0,
            &mQueue);

#define BUFSIZE (4*2*1000)
    int i;
    for (i = 0; i < n_buffers; i++) {
        AudioQueueAllocateBuffer(mQueue, BUFSIZE, &mBuffers[i]);
        AudioQueueEnqueueBuffer(mQueue, mBuffers[i], 0, NULL);
    }

    AudioQueueStart(mQueue, NULL);

    //AudioQueueStop(mQueue, true);
}

double t = 0.0;
double last_x = 0, last_y = 0;


void display(void) {
    if (rptr==wptr) {
        glutPostRedisplay();
        return;
    }

    glDrawBuffer(GL_BACK);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    int n;
    double x, y;
    glBegin(GL_LINES);
    glColor3f(0,1,0);
    glVertex2f(last_x, last_y);

    n = 0;
    while (rptr != wptr && n++ < 1000) {
        x = ringbuf[rptr++] / 32768.0;
        y = ringbuf[rptr++] / 32768.0;
        rptr += 2;
        if (rptr>=RING_SIZE)
            rptr = 0;
        glVertex2f(x, y);
        glVertex2f(x, y);
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

int main(int argc, char **argv) {
    glutInit(&argc, argv);
    glutInitDisplayMode(GLUT_RGB | GLUT_DEPTH | GLUT_DOUBLE | GLUT_ACCUM);
    glutInitWindowSize(600,600);
    glutCreateWindow("Hello World");
    glutDisplayFunc(display);

    long sync = 1;
    //CGLSetParameter (CGLGetCurrentContext(), kCGLCPSwapInterval, &sync);  
    


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
