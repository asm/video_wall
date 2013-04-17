/***

Usage: ./video_wall <remote ip> <cam #> <cam #> <cam #> <flip ([0|1])>

Cameras are integers starting at 0.  The order on osx seems to be random so
you'll just have to play with the values.  Flip is an option to mirror the
video (about the y-axis) since some webcams will do this automatically.

Press 'c' in the video windows to exit.

TODO: this code leaks pretty quickly so don't run it forever :)

***/

#include "opencv/cv.h"
#include "opencv2/objdetect/objdetect.hpp"
#include "opencv2/highgui/highgui.hpp"
#include "opencv2/imgproc/imgproc.hpp"
#include "opencv2/highgui/highgui_c.h"

#include <iostream>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <sys/ioctl.h>

using namespace std;
using namespace cv;

typedef struct thread_args_t {
  int port;
  int sock;
  IplImage **frame;
  IplImage **frame1;
  IplImage **frame2;
  IplImage **frame3;
  int *perspective;
} thread_args_t;

/** Function Headers */
bool detectMotion(IplImage *frame, IplImage *last_frame);
bool transmit_frame(int sock, IplImage *frame);
int tcp_server_socket(unsigned short port);
void *server_thread(void *thread_args);
void handle_tcp_client(int sock, thread_args_t *thread_args);
void spawn_server_thread(thread_args_t thread_args);
void connect_frame(char *ip, int port, IplImage **frame, int *current_perspective);

RNG rng(12345);

int main(int argc, char *argv[]) {
  char *pair_addr = argv[1];
  bool first = true;

  CvCapture* capture1;
  CvCapture* capture2;
  CvCapture* capture3;

  IplImage *frame1, *frame1_last;
  IplImage *frame2, *frame2_last;
  IplImage *frame3, *frame3_last;

  IplImage *current_frame;
 
  int current_perspective = 0;

  ///////////////////////////////////////////////////
  capture1 = cvCaptureFromCAM(atoi(argv[2]));
  cvSetCaptureProperty(capture1, CV_CAP_PROP_FRAME_WIDTH, 320);
  cvSetCaptureProperty(capture1, CV_CAP_PROP_FRAME_HEIGHT, 240);
  printf("opened camera 1 with %fx%f\n", cvGetCaptureProperty(capture1, CV_CAP_PROP_FRAME_WIDTH), cvGetCaptureProperty(capture1, CV_CAP_PROP_FRAME_HEIGHT));

  capture2 = cvCaptureFromCAM(atoi(argv[3]));
  cvSetCaptureProperty(capture2, CV_CAP_PROP_FRAME_WIDTH, 320);
  cvSetCaptureProperty(capture2, CV_CAP_PROP_FRAME_HEIGHT, 240);

  printf("opened camera 2 with %fx%f\n", cvGetCaptureProperty(capture2, CV_CAP_PROP_FRAME_WIDTH), cvGetCaptureProperty(capture2, CV_CAP_PROP_FRAME_HEIGHT));
  capture3 = cvCaptureFromCAM(atoi(argv[4]));
  cvSetCaptureProperty(capture3, CV_CAP_PROP_FRAME_WIDTH, 320);
  cvSetCaptureProperty(capture3, CV_CAP_PROP_FRAME_HEIGHT, 240);
  printf("opened camera 3 with %fx%f\n", cvGetCaptureProperty(capture3, CV_CAP_PROP_FRAME_WIDTH), cvGetCaptureProperty(capture3, CV_CAP_PROP_FRAME_HEIGHT));
  ///////////////////////////////////////////////////


  if(capture1 && capture2 && capture3) {
    CvSize scaled_size = {800, 600};
    bool flip = atoi(argv[5]) == 1 ? true : false;
    IplImage *last_current_frame;
    IplImage *blended_frame;
    int fade_counter = 10;

    for(;;) {
      frame1 = cvQueryFrame(capture1);
      frame2 = cvQueryFrame(capture2);
      frame3 = cvQueryFrame(capture3);

      if (first) {
        if (flip) {
          cvFlip(frame1, frame1, 1);
          cvFlip(frame2, frame2, 1);
          cvFlip(frame3, frame3, 1);
        }
        current_frame      = cvCloneImage(frame1);
        last_current_frame = cvCloneImage(frame1);

        frame1_last = cvCreateImage(cvGetSize(frame1), IPL_DEPTH_8U, 1);
        frame2_last = cvCreateImage(cvGetSize(frame2), IPL_DEPTH_8U, 1);
        frame3_last = cvCreateImage(cvGetSize(frame3), IPL_DEPTH_8U, 1);


        cvCvtColor(frame1, frame1_last, CV_RGB2GRAY);
        cvCvtColor(frame2, frame2_last, CV_RGB2GRAY);
        cvCvtColor(frame3, frame3_last, CV_RGB2GRAY);

        thread_args_t server_args;
        server_args.port = 8888;
        server_args.frame1 = &frame1;
        server_args.frame2 = &frame2;
        server_args.frame3 = &frame3;

        spawn_server_thread(server_args);
        connect_frame(pair_addr, 8888, &current_frame, &current_perspective);

        first = false;
      } else {
        IplImage *scaled_current_frame;

        if (detectMotion(frame1, frame1_last)) {
          if (current_perspective != 2) {
            printf("switching to perspective 2\n");
            last_current_frame = cvCloneImage(current_frame);
            fade_counter = 10;
            current_perspective = 2;
          }
        } 
        if (detectMotion(frame2, frame2_last)) {
          if (current_perspective != 1) {
            printf("switching to perspective 1\n");
            last_current_frame = cvCloneImage(current_frame);
            fade_counter = 10;
            current_perspective = 1;
          }
        }
        if (detectMotion(frame3, frame3_last)) {
          if (current_perspective != 0) {
            printf("switching to perspective 0\n");
            last_current_frame = cvCloneImage(current_frame);
            fade_counter = 10;
            current_perspective = 0;
          }
        }

        imshow("cam1", cv::cvarrToMat(frame1));
        imshow("cam2", cv::cvarrToMat(frame2));
        imshow("cam3", cv::cvarrToMat(frame3));

        if (fade_counter > 0) {
          CvSize frame_size = cvGetSize(current_frame);
          blended_frame = cvCreateImage(frame_size, current_frame->depth, current_frame->nChannels);
          cvAddWeighted(last_current_frame, fade_counter/10.0, current_frame, 1 - (fade_counter/10.0), 0.0, blended_frame);
          fade_counter--;
        } else {
          blended_frame = cvCloneImage(current_frame);
        }
        scaled_current_frame = cvCreateImage(scaled_size, current_frame->depth, current_frame->nChannels);
        cvResize(blended_frame, scaled_current_frame, CV_INTER_CUBIC);
        imshow("current", cv::cvarrToMat(scaled_current_frame));

        cvCvtColor(frame1, frame1_last, CV_RGB2GRAY);
        cvCvtColor(frame2, frame2_last, CV_RGB2GRAY);
        cvCvtColor(frame3, frame3_last, CV_RGB2GRAY);
      }

      int c = waitKey(10);
      if ((char)c == 'c')
        break;
    }
  }
  return 0;
}

bool transmit_frame(int sock, IplImage *frame) {
  if (send(sock, frame->imageData, frame->imageSize, 0) < 0)
    return false;

  return true;
}

bool detectMotion(IplImage *frame, IplImage *last_frame) {
  IplImage *gray_frame, *frame_diff;

  CvSize frame_size = cvGetSize(frame);

  gray_frame = cvCreateImage(frame_size, IPL_DEPTH_8U, 1);
  frame_diff = cvCreateImage(frame_size, IPL_DEPTH_8U, 1);

  cvCvtColor(frame, gray_frame, CV_RGB2GRAY);
  // This can help with noise
  //cvThreshold(gray_frame, gray_frame, 100, 255, CV_THRESH_BINARY);

  cvAbsDiff(gray_frame, last_frame, frame_diff);
  cvThreshold(frame_diff, frame_diff, 10, 255, CV_THRESH_BINARY);
  cvErode(frame_diff,  frame_diff, 0, 2);
  cvDilate(frame_diff, frame_diff, 0, 1);

  CvScalar sum = cvSum(frame_diff);

  if ((sum.val[0] / (frame_size.height * frame_size.width)) > 1.0)
    return true;
  
  return false;
}

int tcp_server_socket(unsigned short port) {
    int sock;
    struct sockaddr_in addr;

    /* Create socket for incoming connections */
    if ((sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0)
      perror("socket");
      
    /* Construct local address structure */
    memset(&addr, 0, sizeof(addr));   /* Zero out structure */
    addr.sin_family = AF_INET;                /* Internet address family */
    addr.sin_addr.s_addr = htonl(INADDR_ANY); /* Any incoming interface */
    addr.sin_port = htons(port);              /* Local port */

    /* Bind to the local address */
    if (bind(sock, (struct sockaddr *) &addr, sizeof(addr)) < 0)
      perror("bind");

    /* Mark the socket so it will listen for incoming connections */
    if (listen(sock, 5) < 0)
      perror("listen");

    return sock;
}

int accept_tcp_connection(int s_sock)
{
    int c_sock;                  /* Socket descriptor for client */
    struct sockaddr_in addr;     /* Client address */
    unsigned int len;            /* Length of client address data structure */

    /* Set the size of the in-out parameter */
    len = sizeof(addr);
    
    /* Wait for a client to connect */
    if ((c_sock = accept(s_sock, (struct sockaddr *) &addr, &len)) < 0)
        perror("accept() failed");
    
    /* clntSock is connected to a client! */
    
    printf("Handling client %s\n", inet_ntoa(addr.sin_addr));

    return c_sock;
}

void spawn_server_thread(thread_args_t thread_args) {
  pthread_t thread;

  /* Create client thread */
  if (pthread_create(&thread, NULL, server_thread, &thread_args) != 0)
    perror("pthread_create() failed");
}

void *server_thread(void *thread_args) {
  int s_sock;
  int c_sock;

  pthread_detach(pthread_self()); 

  s_sock = tcp_server_socket(((thread_args_t *)thread_args)->port);

  printf("server thread listening on %i\n", ((thread_args_t *)thread_args)->port);

  for (;;) {
    c_sock = accept_tcp_connection(s_sock);
    handle_tcp_client(c_sock, (thread_args_t*)thread_args);
  }
}

void *client_thread(void *thread_args) {
  int sock, bytes_received, width;
  char *buf;
  pthread_t *current_thread;

  IplImage **frame;
  int my_perspective;

  pthread_detach(pthread_self()); 

  sock  = ((struct thread_args_t *) thread_args) -> sock;
  frame = ((struct thread_args_t *) thread_args) -> frame;

  width = (*frame)->widthStep;

  if ((buf = (char *)malloc((*frame)->imageSize)) == NULL)
    perror("malloc() failed");

  for(;;) {
    if (my_perspective != *((struct thread_args_t *) thread_args) -> perspective) {
      my_perspective = *((struct thread_args_t *) thread_args) -> perspective;
      printf("sending new perspective %i\n", my_perspective);
      if (send(sock, &my_perspective, sizeof(my_perspective), 0) < 0)
        perror("send()");
    }
    bytes_received = 0;

    for (;;) {
      int to_go = (*frame)->imageSize - bytes_received;

      if ((bytes_received += recv(sock, buf + bytes_received, to_go > 1024 ? 1024 : to_go, 0)) <= 0) {
        perror("recv");
        return NULL;
      }

      if (bytes_received == (*frame)->imageSize) {
        memcpy((*frame)->imageData, buf, (*frame)->imageSize);
        break;
      }
    }

  }
}

void handle_tcp_client(int sock, thread_args_t *thread_args) {
  int bytes_waiting = 0;
  int perspective_request = 0;

  IplImage *frames[] = {
    *(thread_args->frame1),
    *(thread_args->frame2),
    *(thread_args->frame3),
  };

  for(;;) {
    if (!transmit_frame(sock, frames[perspective_request])) {
      return;
    }

    ioctl(sock, FIONREAD, &bytes_waiting);

    if (bytes_waiting > 0) {
      if (recv(sock, &perspective_request, sizeof(perspective_request), 0) <= 0)
        perror("recv()");
    }
  }
}

void connect_frame(char *ip, int port, IplImage **frame, int *perspective) {
  int sock;
  struct sockaddr_in addr;
  pthread_t thread;
  struct thread_args_t *thread_args;
  int success = -1;

  while(success < 0) {
    if ((sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0)
      perror("socket() failed");

    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = inet_addr(ip);
    addr.sin_port        = htons(port);

    if ((success = connect(sock, (struct sockaddr *) &addr, sizeof(addr))) < 0) {
      perror("connect() failed, retrying");
      sleep(1);
    }
  }

  /* Create separate memory for client argument */
  if ((thread_args = (struct thread_args_t *) malloc(sizeof(struct thread_args_t))) == NULL)
    perror("malloc() failed");
  thread_args->sock = sock;
  thread_args->frame = frame;
  thread_args->perspective = perspective;

  /* Create client thread */
  if (pthread_create(&thread, NULL, client_thread, (void *)thread_args) != 0)
    perror("pthread_create() failed");

  printf("client thread connecting to %s:%i\n", ip, port);
}
