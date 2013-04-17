all: video_wall

video_wall: video_wall.cpp
	g++ video_wall.cpp -lopencv_core -lopencv_imgproc -lopencv_highgui -lopencv_objdetect  -O3 -o video_wall

clean:
	rm video_wall
