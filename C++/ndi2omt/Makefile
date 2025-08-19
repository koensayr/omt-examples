# NDI to OMT Converter Makefile
# Supports macOS and Linux builds

# Detect OS
UNAME_S := $(shell uname -s)

# Project settings
TARGET = ndi_to_omt_converter
SOURCES = ndi_to_omt_converter.cpp

# Compiler settings
CXX = g++
CXXFLAGS = -std=c++11 -O2 -Wall -Wextra

# Platform-specific settings
ifeq ($(UNAME_S),Darwin)
    # macOS settings
    NDI_SDK_PATH = "/Library/NDI Advanced SDK for Apple"
    NDI_INCLUDE = $(NDI_SDK_PATH)/include
    NDI_LIB_PATH = $(NDI_SDK_PATH)/lib/macOS
    NDI_LIBS = -lndi_advanced
    
    # OMT library (local .dylib file)
    OMT_LIBS = ./libomt.dylib
    
    # Homebrew paths (for any additional dependencies)
    HOMEBREW_INCLUDE = /opt/homebrew/include
    HOMEBREW_LIB = /opt/homebrew/lib
    
    # System libraries
    SYS_LIBS = -lpthread -framework CoreFoundation -framework IOKit
    
    # Rpath for runtime library loading
    LDFLAGS = -Wl,-rpath,$(NDI_LIB_PATH)
    
else ifeq ($(UNAME_S),Linux)
    # Linux settings
    NDI_SDK_PATH = "/usr/local/NDI SDK Advanced"
    NDI_INCLUDE = $(NDI_SDK_PATH)/include
    NDI_LIB_PATH = $(NDI_SDK_PATH)/lib/x86_64-linux-gnu
    NDI_LIBS = -lndi_advanced
    
    # OMT library (local .so file)
    OMT_LIBS = ./libomt.so
    
    # System libraries
    SYS_LIBS = -lpthread -ldl
    
    # Rpath for runtime library loading
    LDFLAGS = -Wl,-rpath,$(NDI_LIB_PATH)
    
else
    $(error Unsupported platform: $(UNAME_S))
endif

# Include paths
INCLUDES = -I$(NDI_INCLUDE) -I. -I$(HOMEBREW_INCLUDE)

# Library paths  
LIB_PATHS = -L$(NDI_LIB_PATH) -L$(HOMEBREW_LIB)

# All libraries
LIBS = $(NDI_LIBS) $(OMT_LIBS) $(SYS_LIBS)

# Build rules
.PHONY: all clean install help

all: $(TARGET)

$(TARGET): $(SOURCES)
	@echo "Building $(TARGET) for $(UNAME_S)..."
	@echo "NDI SDK Path: $(NDI_SDK_PATH)"
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(LIB_PATHS) $(LDFLAGS) -o $@ $^ $(LIBS)
	@echo "Build complete!"

clean:
	@echo "Cleaning..."
	rm -f $(TARGET)
	@echo "Clean complete!"

install: $(TARGET)
ifeq ($(UNAME_S),Darwin)
	@echo "Installing to /usr/local/bin..."
	sudo cp $(TARGET) /usr/local/bin/
	@echo "Install complete!"
else
	@echo "Installing to /usr/local/bin..."
	sudo cp $(TARGET) /usr/local/bin/
	@echo "Install complete!"
endif

# Development targets
debug: CXXFLAGS += -g -DDEBUG
debug: $(TARGET)

verbose: CXXFLAGS += -v
verbose: $(TARGET)

# Check dependencies
check-deps:
	@echo "Checking dependencies..."
	@echo "NDI SDK Path: $(NDI_SDK_PATH)"
	@if [ ! -d $(NDI_SDK_PATH) ]; then \
		echo "ERROR: NDI Advanced SDK not found at $(NDI_SDK_PATH)"; \
		echo "Please install NDI Advanced SDK"; \
		exit 1; \
	fi
	@if [ ! -f $(NDI_INCLUDE)/Processing.NDI.Advanced.h ]; then \
		echo "ERROR: NDI Advanced SDK headers not found"; \
		exit 1; \
	fi
	@if [ ! -d $(NDI_LIB_PATH) ]; then \
		echo "ERROR: NDI Advanced SDK libraries not found at $(NDI_LIB_PATH)"; \
		exit 1; \
	fi
	@if [ ! -f ./libomt.h ]; then \
		echo "ERROR: libomt.h not found in current directory"; \
		echo "Please ensure libomt.h is in the project directory"; \
		exit 1; \
	fi
ifeq ($(UNAME_S),Darwin)
	@if [ ! -f ./libomt.dylib ]; then \
		echo "ERROR: libomt.dylib not found in current directory"; \
		echo "Please ensure libomt.dylib is in the project directory"; \
		exit 1; \
	fi
else
	@if [ ! -f ./libomt.so ]; then \
		echo "ERROR: libomt.so not found in current directory"; \
		echo "Please ensure libomt.so is in the project directory"; \
		exit 1; \
	fi
endif
	@echo "Dependencies OK!"

help:
	@echo "NDI to OMT Converter Build System"
	@echo "================================="
	@echo ""
	@echo "Targets:"
	@echo "  all        - Build the converter (default)"
	@echo "  clean      - Remove build artifacts"
	@echo "  install    - Install to system PATH"
	@echo "  debug      - Build with debug symbols"
	@echo "  verbose    - Build with verbose output"
	@echo "  check-deps - Verify all dependencies"
	@echo "  help       - Show this help"
	@echo ""
	@echo "Usage:"
	@echo "  make                    # Build normally"
	@echo "  make debug              # Build with debug info"
	@echo "  make check-deps         # Verify NDI SDK installation"
	@echo "  make clean && make      # Clean rebuild"
	@echo ""
	@echo "Requirements:"
	@echo "  - NDI Advanced SDK installed at $(NDI_SDK_PATH)"
	@echo "  - libomt.h and libomt.dylib (macOS) or libomt.so (Linux) in current directory"
	@echo "  - g++ with C++11 support"

# Show current configuration
config:
	@echo "Build Configuration:"
	@echo "==================="
	@echo "Platform: $(UNAME_S)"
	@echo "Target: $(TARGET)"
	@echo "Compiler: $(CXX)"
	@echo "Flags: $(CXXFLAGS)"
	@echo "NDI SDK: $(NDI_SDK_PATH)"
	@echo "NDI Include: $(NDI_INCLUDE)"
	@echo "NDI Lib: $(NDI_LIB_PATH)"
	@echo "Libraries: $(LIBS)"
