NAME			= tagtool
CXX				= g++
CXXFLAGS		= -Wall -Wextra -Werror -O3
CPPFLAGS		= -Iincludes
SRCS_DIR		= srcs
OBJS_DIR		= objs

SRCS			=	$(SRCS_DIR)/main.cpp \
					$(SRCS_DIR)/Utils.cpp

OBJS			= $(SRCS:$(SRCS_DIR)/%.cpp=$(OBJS_DIR)/%.o)


OPENCV_CFLAGS	= $(shell pkg-config --cflags opencv4 2>/dev/null)
OPENCV_LDFLAGS	=	-lopencv_core \
					-lopencv_imgcodecs \
					-lopencv_img_hash

TAGLIB_CFLAGS	= $(shell pkg-config --cflags taglib)
TAGLIB_LDFLAGS	= $(shell pkg-config --libs taglib)

ifeq ($(strip $(OPENCV_CFLAGS)),)
$(error "opencv4 not found. Cannot compile without OpenCV.")
endif

ifeq ($(strip $(TAGLIB_CFLAGS)),)
$(error "taglib not found. Cannot compile without TagLib.")
endif

all: $(NAME)

$(OBJS_DIR)/%.o: $(SRCS_DIR)/%.cpp
	@mkdir -p $(OBJS_DIR)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) $(OPENCV_CFLAGS) $(TAGLIB_CFLAGS) -c $< -o $@

$(NAME): $(OBJS)
	$(CXX) $(CXXFLAGS) $(OBJS) -o $@ $(OPENCV_LDFLAGS) $(TAGLIB_LDFLAGS)

clean:
	rm -rf $(OBJS_DIR)

fclean: clean
	rm -f $(NAME)

re: fclean all
