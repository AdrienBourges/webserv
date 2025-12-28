# **************************************************************************** #
#                                                                              #
#                                                         :::      ::::::::    #
#    Makefile                                           :+:      :+:    :+:    #
#                                                     +:+ +:+         +:+      #
#    By: you <you@student.42.fr>                    +#+  +:+       +#+         #
#                                                 +#+#+#+#+#+   +#+            #
#    Created: 2025/11/24  by you                      #+#    #+#              #
#    Updated: 2025/11/24  by you                      ###   ########.fr        #
#                                                                              #
# **************************************************************************** #

# Name of the final executable
NAME        = webserv

# Compiler and flags
# -std=c++98 is required by the subject
CXX         = c++
CXXFLAGS    = -Wall -Wextra -Werror -std=c++98

# Folders
SRCDIR      = src
INCDIR      = include

# Source files for now: only main.cpp
# We will add more .cpp files here in later steps.
SRCS        = $(SRCDIR)/main.cpp \
			  $(SRCDIR)/WebServer.cpp \
 		      $(SRCDIR)/HttpRequest.cpp \
 		      $(SRCDIR)/HttpResponse.cpp \
			  $(SRCDIR)/Config.cpp

# Object files (same names, but .o extension)
OBJS        = $(SRCS:.cpp=.o)

# Command to remove files
RM          = rm -f

# Default rule: build the executable
all: $(NAME)

# Link step:
# If none of the object files changed, this rule won't run,
# so there is no unnecessary relinking (as required by 42).
$(NAME): $(OBJS)
	$(CXX) $(CXXFLAGS) $(OBJS) -o $(NAME)

# Generic rule to compile any .cpp into a .o
# -I$(INCDIR) tells the compiler where to find our headers (we'll use it later).
$(SRCDIR)/%.o: $(SRCDIR)/%.cpp
	$(CXX) $(CXXFLAGS) -I$(INCDIR) -c $< -o $@

# Remove compiled object files
clean:
	$(RM) $(OBJS)

# Remove objects + executable
fclean: clean
	$(RM) $(NAME)

# Rebuild everything from scratch
re: fclean all

# Mark these targets as "phony" so make doesn't confuse them with real files
.PHONY: all clean fclean re

