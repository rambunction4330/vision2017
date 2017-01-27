#include <sys/types.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <semaphore.h>
#include <iostream>
#include "opencv2/opencv.hpp"

#define PORTNUMBER  9001 
#define DONOTKNOW 10000000

using namespace std;
using namespace cv;

//added for further changes
int iLowH = 31;
int iHighH = 97;
int iLowS = 100; 
int iHighS = 255;
int iLowL = 70;
int iHighL = 255;
int fr = 10;
int xres = 1920;
int yres = 1080;
double cameraAngle = 70.42;
double yCameraAngle = (cameraAngle*9)/16;
double relativeBearing = DONOTKNOW;
double globalYAngle = DONOTKNOW;
pthread_mutex_t dataLock;

// forward declaration of functions
void *handleClient(void *arg);
void receiveNextCommand(char*, int);
void *capture(void *arg);

int main(void)
{
  
  int n, s;
  socklen_t len;
  int max;
  int number;
  struct sockaddr_in name;
  pthread_mutex_init(&dataLock, NULL);

  // create the socket
  if ( (s = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    perror("socket");
    exit(1);
  }

  memset(&name, 0, sizeof(struct sockaddr_in));
  name.sin_family = AF_INET;
  name.sin_port = htons(PORTNUMBER);
  len = sizeof(struct sockaddr_in);

  // listen on all network interfaces
  n = INADDR_ANY;
  memcpy(&name.sin_addr, &n, sizeof(long));

  int reuse = 1;
  if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse)) < 0) {
    perror("setsockopt(SO_REUSEADDR)");
    exit(1);
  }

  if (setsockopt(s, SOL_SOCKET, SO_KEEPALIVE, (const char*)&reuse, sizeof(reuse)) < 0) {
    perror("setsockopt(SO_KEEPALIVE)");
    exit(1);
  }

  // bind socket the network interface
  if (bind(s, (struct sockaddr *) &name, len) < 0) {
    perror("bind");
    exit(1);
  }

  // listen for connections
  if (listen(s, 5) < 0) {
    perror("listen");
    exit(1);
  }
 VideoCapture capture(0);
  capture.set(CV_CAP_PROP_FRAME_WIDTH, xres);
  capture.set(CV_CAP_PROP_FRAME_HEIGHT, yres);
  capture.set(CV_CAP_PROP_FPS, fr);
  if(!capture.isOpened()) {
    cout << "Failed to connect to the camera." << endl;
  }
  Mat frame, dst, hsv, binary, tmpBinary;
  //Ideal shape of high goal reflective tape.
  std::vector<Point> shape;
  shape.push_back(Point2d(0,0));
  shape.push_back(Point2d(0,12));
  shape.push_back(Point2d(2,12));
  shape.push_back(Point2d(2,2));
  shape.push_back(Point2d(18,2));
  shape.push_back(Point2d(18,12));
  shape.push_back(Point2d(20,12));
  shape.push_back(Point2d(20,0));
  shape.push_back(Point2d(0,0));
  while(true) {
    
    capture >> dst;
    if(dst.empty()) {
      cout << "failed to capture an image" << endl;
    }
    
    resize(dst ,frame, frame.size(), .35, .35, INTER_AREA);
  pthread_t captureThreadId;
  int i = 3;
  int captureThread = pthread_create(&captureThreadId, NULL, capture, (void*)&i);

  // it is important to detach the thread to avoid memory leak
  pthread_detach(captureThreadId);

  while(true) {

    // block until get a request
    int ns = accept(s, (struct sockaddr *) &name, &len);

    if ( ns < 0 ) {
      perror("accept");
      exit(1);
    }

    // each client connection is handled by a seperate thread since
    // a single client can hold a connection open indefinitely making multiple
    // data requests prior to closing the connection
    pthread_t threadId;
    int thread = pthread_create(&threadId, NULL, handleClient, (void*) &ns);
    // it is important to detach the thread to avoid a memory leak
    pthread_detach(threadId);
  } 
  
  close(s);
  exit(0);
}

void *capture(void *arg) {  
while(true){   
    cvtColor(frame, hsv, CV_BGR2HLS);
    inRange(hsv, Scalar(iLowH, iLowS, iLowL), Scalar(iHighH, iHighS, iHighL), binary);

    std::vector < std::vector<Point> > contours;
    tmpBinary = binary.clone();
    findContours(tmpBinary, contours, RETR_LIST, CHAIN_APPROX_NONE);
    tmpBinary.release();
    
    double bestShapeMatch = DONOTKNOW;
    Point2d pointOfBestShapeMatch;
    double minimumArea = 350;
    bool foundBestMatch = false;    

    for (size_t contourIdx = 0; contourIdx < contours.size(); contourIdx++) {

      const Moments moms = moments(Mat(contours[contourIdx]));
 
      // filter blobs which are too small
      double area = moms.m00;
      if ( area < minimumArea ) {
        continue;
      }
      double shapematch = matchShapes(shape, contours[contourIdx], CV_CONTOURS_MATCH_I2, 0);
      //A perfect match would be 0.
      //Smaller is a better match.
      if(shapematch > 4){
		  continue;
	  }
	  cout << "match value = " << shapematch << endl;
      if(shapematch < bestShapeMatch){
        bestShapeMatch = shapematch;
        pointOfBestShapeMatch = Point2d(moms.m10 / moms.m00, moms.m01 / moms.m00);
        foundBestMatch = true;
      }
    
    }
    
    double angle = DONOTKNOW;
    double yAngle = DONOTKNOW;
    if (foundBestMatch) {
      angle = (pointOfBestShapeMatch.x - (672/2))*cameraAngle/672;
      yAngle = ((378/2) - pointOfBestShapeMatch.y )*yCameraAngle/672;
      cout << "x = "  << pointOfBestShapeMatch.x << endl;
      cout << "xangle = " << angle << endl;
      cout << "yAngle = " << yAngle << endl; 
    }
  
    // obtain the lock and copy the data
    pthread_mutex_lock(&dataLock);
    relativeBearing = angle;
    globalYAngle = yAngle;
    pthread_mutex_unlock(&dataLock);
  }
}
}


void *handleClient(void *arg) {
  // printf("Thread starting\n");
  int ns = *((int*) arg);
  char sendbuffer[1024];
  char command[128];

  // start conversation with client
  while(true) {

    receiveNextCommand(command, ns);

    if ( strcmp(command, "STOP") == 0 ) {
      //printf("Received STOP command\n");
      break;
    } else if ( strcmp(command, "DATA") == 0 ) {
      //printf("Received DATA command\n");

      // obtain the lock and copy the data
      pthread_mutex_lock(&dataLock);
      double copyRelativeBearing = relativeBearing;
      double copyGlobalYAngle = globalYAngle;
      pthread_mutex_unlock(&dataLock);
      
      // the protocol will send an empty line when the data transfer is complete
      int sendbufferLen = -1;
      if ( copyRelativeBearing == DONOTKNOW ) {
        sendbufferLen = sprintf(sendbuffer, "\n");
      } else {
        sendbufferLen = sprintf(sendbuffer, "rb=%.1f\nya=%.1f\n\n", copyRelativeBearing, copyGlobalYAngle);
      }

      // write response to client
      write(ns, sendbuffer, sendbufferLen);
    } else {
      //printf("Received unknown command '%s'\n", command);
      break;
    }

  }
  close(ns);
  //printf("Thread ending\n");
  return 0;
}

void receiveNextCommand(char *command, int ns) {
  int receiveLength = read(ns, command, 1024);
  int commandLength = 0;
  while(commandLength < receiveLength) {
    char value = command[commandLength];
    if ( value == 0x0d || value == 0x0a ) {
      break;
    } 
    commandLength++;
  }

  // add the terminating 0 to mark the end of the string value in the char *
  command[commandLength] = 0;
}

