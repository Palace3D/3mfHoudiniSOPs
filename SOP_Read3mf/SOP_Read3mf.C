/*
 * Copyright (c) 2025
 *	Side Effects Software Inc.  All rights reserved.
 *
 * Redistribution and use of Houdini Development Kit samples in source and
 * binary forms, with or without modification, are permitted provided that the
 * following conditions are met:
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. The name of Side Effects Software may not be used to endorse or
 *    promote products derived from this software without specific prior
 *    written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY SIDE EFFECTS SOFTWARE `AS IS' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN
 * NO EVENT SHALL SIDE EFFECTS SOFTWARE BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA,
 * OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *----------------------------------------------------------------------------
 * The Read3mf SOP.  This SOP reads in a geometry from the filesystem in the 3mf format.
 * We try to adhere to The 3mf Core Specification and most of the 3mf Materials
 * Specification: https://github.com/3mfconsortium
 */

#include <GU/GU_Detail.h>
#include <OP/OP_Operator.h>
#include <OP/OP_Director.h>
#include <OP/OP_Network.h>
#include <OP/OP_AutoLockInputs.h>
#include <OP/OP_OperatorTable.h>
#include <OP/OP_Parameters.h>
#include <CH/CH_Manager.h>
#include <PRM/PRM_Include.h>
#include <UT/UT_DSOVersion.h>
#include <cstddef>
#include <string>
#include <filesystem>
//#include <format> // This version of C++ is too old to use this, alas.
#include <iostream>
#include <fstream>
#include <sstream>
#include <chrono>
#include <ctime>
#include <SOP/SOP_Node.h>
#include <UT/UT_Console.h>
#include <UT/UT_String.h>
#include <UT/UT_PtrArray.h>
#include <GU/GU_PrimPoly.h>
#include <GEO/GEO_Primitive.h>
#include <GEO/GEO_PrimPoly.h> // Do I need this if I'm not just doing my simple test?
#include <GA/GA_Types.h>
#include <GA/GA_MergeOptions.h>
#include <VOP/VOP_Node.h>
//#include <UT/UT_Error.h>
#include <PY/PY_Python.h>
#include <filesystem>
#include <sys/stat.h>
#include <minizip/zip.h>
#include <minizip/ioapi.h>
#include <zlib.h> 
#include "tinyxml2.h"
//#include <boost/uuid/uuid.hpp>
//#include <boost/uuid/uuid_generators.hpp>
//#include <boost/uuid/uuid_io.hpp>
#define STB_IMAGE_IMPLEMENTATION // For image loading
#include "stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION // For image writing
#include "stb_image_write.h"
#include <cstdlib> // for free()
#include <unordered_set>
#include <limits.h>

#include "SOP_Read3mf.h"

namespace fs = std::filesystem;
using namespace HDK_Sample;
using namespace tinyxml2;


namespace HDK_Sample {

// I use a "snap to grid" approach to make sure my hash operators such as
// < and == behave rationally with each other. The idea is that within some
// small tolerance, vertices that have some small fluff to their positions but
// really want to associate witht he same point should be able to do so.
struct PointKey {
    UT_Vector3 pos;

    // We use a snap function that mimics Houdini's internal rounding
    void getSnapped(long long &ix, long long &iy, long long &iz) const {
        // We use 1.0 / tolerance to get the number of "grid units" per 1 unit of space
        // Using a double here prevents precision loss during the scaling
        const double scale = 1.0 / SYS_FTOLERANCE;
        
        ix = static_cast<long long>(std::floor(pos.x() * scale + 0.5));
        iy = static_cast<long long>(std::floor(pos.y() * scale + 0.5));
        iz = static_cast<long long>(std::floor(pos.z() * scale + 0.5));
    }
   
    // Add this for UT_Map compatibility
    size_t hash() const {
        size_t seed = 0;
        long long x, y, z;
        getSnapped(x, y, z);
        SYShashCombine(seed, x);
        SYShashCombine(seed, y);
        SYShashCombine(seed, z);

        return seed;
    }

    bool operator==(const PointKey& other) const {
        long long x1, y1, z1, x2, y2, z2;
        getSnapped(x1, y1, z1);
        other.getSnapped(x2, y2, z2);

        return (x1 == x2 && y1 == y2 && z1 == z2);
    }
};


// This tells hboost (and UT_Map) how to use our hash() method
inline size_t hash_value(const PointKey& key) {
    return key.hash();
}


// The << operator for debug logging with PointKey. LOG_DEBUG needs to see this, so we keep it here.
std::ostream& operator<<(std::ostream& os, const PointKey& pk) {
    // Since UT_Vector3 has an operator<<, we can use pk.pos directly
    os << "{pos: " << pk.pos << "}";
    return os;
}
} // end HDK_Sample

// Provide entry point for installing this SOP. This has to be outside the HDK_Sample namespace so Houdini sees it
void
newSopOperator(OP_OperatorTable *table)
{
    table->addOperator(new OP_Operator(
        "hdk_read3mf",
        "Read3mf",
        SOP_Read3mf::myConstructor,
        SOP_Read3mf::myTemplateList,
        0, // min sources
        0, // max sources
        nullptr
        ));
}

// SOP parameter names.
static PRM_Name names[] = {
    PRM_Name("flip", "Flip Normals"),   // Houdini has the opposite winding order
    PRM_Name("build", "Apply Build Instructions"), // Render only the objects listed in the 3mf build instructions
    PRM_Name("debug", "Debug"),         // Some extra info printed to std::clog
    PRM_Name("timer", "Timer"),         // Record how long it takes to read in the model
    PRM_Name("filename", "File Name"),  // 3mf file name
    PRM_Name("assets", "Asset Folding"),// Folder in which to unpack the 3mf file
    PRM_Name("readBtn", "Read"),        // Read it
};

// SOP parameter defaults
static PRM_Default  flipit(1);  // Houdini winds in opposite order of 3mf
static PRM_Default  buildit(1); // By default, we should apply these
static PRM_Default  debugit(0); // Turn on during development or major debugging
static PRM_Default  timeit(0);  // Record how long it takes to read in the model
static PRM_Default  filen(0, "my-model.3mf");
static PRM_Default  assetsn(0, SOP_Read3mf::EXTRACTFOLDER_DEFAULT.data());


// SOP parameter template
// type, vector size, nameptr, defaultsptr, choiceptr, rangeptr, callbackfunc, ...
// To set help text, though, you need the spareptr to be set to 0 and the the paramgroup set to 1.
PRM_Template
SOP_Read3mf::myTemplateList[] = {
    PRM_Template(PRM_TOGGLE,	1, &names[0], &flipit, 0, 0, 0, 0, 1, "Flip Houdini normals to match 3mf normals."),
    PRM_Template(PRM_TOGGLE,    1, &names[1], &buildit, 0, 0, 0, 0, 1, "Apply 3mf build instructions and render only what is listed in them."),
    PRM_Template(PRM_TOGGLE,    1, &names[2], &debugit, 0, 0, 0, 0, 1, "Print debug information."),
    PRM_Template(PRM_TOGGLE,    1, &names[3], &timeit, 0, 0, 0, 0, 1, "Report the time it took to read in the model."),
    PRM_Template(PRM_FILE_E,	1, &names[4], &filen, 0, 0, 0, 0, 1, "Name of the 3mf input file."),
    PRM_Template(PRM_DIRECTORY_E,    1, &names[5], &assetsn, 0, 0, 0, 0, 1, "Folder in which to unpack the 3mf archive."),
    PRM_Template(PRM_CALLBACK,  1, &names[6], 0, 0, 0, &SOP_Read3mf::read, 0, 1, "Read in the 3mf model."),
    PRM_Template(),
};


/*
 * --------------------------------------------------------------------------------
 * UTILITY FUNCTIONS
 * --------------------------------------------------------------------------------
 */

namespace HDK_Sample {
//
// Utility function to ensure all parent directories exist for a given file path.
// Return true if the directories were created or already exist.
//
bool
createAllDirectories(const std::string& dir_path) {
    // Convert the dir path string to a std::filesystem::path object.
    fs::path p(dir_path);

    // Create the directories recursively.
    try {
        if (!fs::create_directories(dir_path)) {
            // Check if the directory creation failed, and it truly doesn't exist.
            if (!fs::exists(dir_path)) {
                return false;
            }
            // Otherwise, creation might have returned false because it already existed,
            // which is fine.
        }
        return true;

    } catch (const fs::filesystem_error& e) {
        // Handle potential errors like permission denied, invalid path, etc.
        std::cerr << "Error: Filesystem Error creating directories: " << e.what() << std::endl;
        return false;
    }
}


//
// Utility function to ensure all parent directories exist for a given file path.
// Return true if the directories were created or already exist.
//
bool
createIntermediateDirectories(const std::string& full_file_path) {
    // Convert the file path string to a std::filesystem::path object.
    fs::path p(full_file_path);
    
    // Extract the directory portion of the path.
    fs::path dir_path = p.parent_path();

    // Create the directories recursively.
    try {
        if (!fs::create_directories(dir_path)) {
            // Check if the directory creation failed, and it truly doesn't exist.
            if (!fs::exists(dir_path)) {
                return false;
            }
            // Otherwise, creation might have returned false because it already existed,
            // which is fine.
        }
        return true;

    } catch (const fs::filesystem_error& e) {
        // Handle potential errors like permission denied, invalid path, etc.
        std::cerr << "Error: Filesystem Error creating directories: " << e.what() << std::endl;
        return false;
    }
}


//
// Unzip archive into a folder.
//
SOP_Read3mf::ErrorCode
unzipArchive(const std::string& filename, const std::string& extractFolder) {
    if (!createAllDirectories(extractFolder)) {
        std::cerr << "Error: Could not create folder into which to unpack the 3mf archive." << std::endl;
        return SOP_Read3mf::ErrorCode::FILE_FAILURE;
    }
    if (!fs::exists(fs::path(filename))) {
        std::cerr << "Error: The 3mf archive file " << filename << " does not exist." << std::endl;
        return SOP_Read3mf::ErrorCode::FILE_FAILURE;
    }

    // Unzip and unpack the 3mf archive
    unzFile zip_file = unzOpen(filename.c_str());
    if (!zip_file) {
        std::cerr << "Error: Cannot open 3mf file " << filename << std::endl;
        return SOP_Read3mf::ErrorCode::ZIP_FAILURE;
    }
    int err = unzGoToFirstFile(zip_file);
    if (err != UNZ_OK) {
        std::cerr << "Error: Failed to go to first file in 3mf archive." << std::endl;
        unzClose(zip_file);
        return SOP_Read3mf::ErrorCode::ZIP_FAILURE;
    }
    do {
        char filename_inzip[256];
        unz_file_info file_info;
        err = unzGetCurrentFileInfo(zip_file, &file_info, filename_inzip, sizeof(filename_inzip), NULL, 0, NULL, 0);
        if (err != UNZ_OK) {
            std::cerr << "Error: Failed to get info for a file in the 3mf archive." << std::endl;
            unzClose(zip_file);
            return SOP_Read3mf::ErrorCode::ZIP_FAILURE;
        }

        std::string filename_str = filename_inzip;
        std::string full_path = extractFolder + "/" + filename_str;
        if (filename_str.back() == '/') { // Directories end with a '/'
            if (!createAllDirectories(full_path)) {
                std::cerr << "Error: Could not create a required path in the unpacked 3mf archive." << std::endl;
                unzClose(zip_file);
                return SOP_Read3mf::ErrorCode::FILE_FAILURE;
            }
        } else {
            if (!createIntermediateDirectories(full_path)) {
                std::cerr << "Error: Could not create intermediate directories for a file when unpacking the 3mf archive." << std::endl;
                unzClose(zip_file);
                return SOP_Read3mf::ErrorCode::FILE_FAILURE;
            }
            err = unzOpenCurrentFile(zip_file); 
            if (err != UNZ_OK) {
                std::cerr << "Error: Could not open file inside 3mf archive." << std::endl;
                unzClose(zip_file);
                return SOP_Read3mf::ErrorCode::ZIP_FAILURE;;
            }
            std::ofstream outfile(full_path, std::ios::binary);
            if (!outfile.is_open()) {
                std::cerr << "Error: Could not create output file for unpacked 3mf archive." << std:: endl;
                unzCloseCurrentFile(zip_file);
                unzClose(zip_file);
                return SOP_Read3mf::ErrorCode::FILE_FAILURE;
            }
            #define READ_BUFFER_SIZE 8192 // Is this what I should do??
            char buffer[READ_BUFFER_SIZE];
            int read_count;
            do {
                read_count = unzReadCurrentFile(zip_file, buffer, READ_BUFFER_SIZE);
                if(read_count < 0) {
                    std::cerr << "Error: Read error on file: " << filename_str << std::endl;
                    outfile.close();
                    unzCloseCurrentFile(zip_file);
                    unzClose(zip_file);
                    return SOP_Read3mf::ErrorCode::ZIP_FAILURE;
                }
                if (read_count > 0) {
                    outfile.write(buffer, read_count);
                }
            } while (read_count > 0);
            outfile.close();
            unzCloseCurrentFile(zip_file);
        }
        err = unzGoToNextFile(zip_file);
    } while (err == UNZ_OK);
    if (err != UNZ_END_OF_LIST_OF_FILE && err != UNZ_OK) {
        std::cerr << "Error: unpacking of 3mf archive failed." << std::endl;
        unzClose(zip_file);
        return SOP_Read3mf::ErrorCode::ZIP_FAILURE;
    }
    unzClose(zip_file);
    return SOP_Read3mf::ErrorCode::SUCCESS;
}


//
// Get a timestamp accurate enough for millisecond measurements.
//
//std::chrono::time_point<std::chrono::system_clock>
auto
generateTimestamp() {
    auto now = std::chrono::high_resolution_clock::now();
    return now;
}


//
// Parse a 12-component 3MF transform string directly into a UT_Matrix4.
// From the 3mf specs:
// When objects need to be transformed for rotation, scaling, or translation purposes, row-
// major affine 3D matrices (4x4) are used. The matrix SHOULD NOT be singular or nearly
// singular. Transforms are of the form, where only the first 3 column values are specified. The last
// column is never provided, and has the fixed values 0.0, 0.0, 0.0, 1.0. (It's "implicit".) When specified as an
// attribute value, matrices have the form "m00 m01 m02 m10 m11 m12 m20 m21 m22 m30
// m31 m32" where each value is a decimal number of arbitrary precision.
//
// Note that Houdini's matrices are stored in row-major format.
//
// Returns true upon success.
//
bool
parseTransformString(const std::string& transformString, UT_Matrix4& matrix) {

    matrix.identity(); // Start with identity matrix (sets the implicit 4th row [0, 0, 0, 1])
    
    std::stringstream st(transformString);
    fpreal value;
    int index = 0;

    while (st >> value && index < 12) {
        // Map the flat index (0-11) to the matrix (row, col) indices:
        int row = index / 3;
        int col = index % 3;
        LOG_DEBUG(false, "    (" + std::to_string(row) + ", " + std::to_string(col) + ")"
            + " has value " + std::to_string(value));
        matrix(row, col) = value;

        index++;
    }

    if (index != 12) {
        std::cerr << "Warning: Read " << index << " transform elements, expected 12." << std::endl;
        return false;
    }
    
    return true;
}


//
// Convert 3mf color format to Houdini's. Returns default color on failure. Includes alpha as
// a parameter. We generally don't need alpha except for the base layer of multi-properties.
//
UT_Vector3
convertHexStringToUTVector3(const std::string& hex_string, float& alpha, bool debug) {
std::string working_hex_string = hex_string;

    // Handle prefixes: '#' and '0x'
    if (working_hex_string.length() > 0 && working_hex_string.front() == '#') {
        working_hex_string.erase(0, 1);
    }
    // Check for "0x" after potentially removing "#"
    if (working_hex_string.length() >= 2 && working_hex_string.substr(0, 2) == "0x") {
        working_hex_string.erase(0, 2);
    }

    // Handle 8-character string (RRGGBBAA): truncate alpha
    // 3mf usually uses RRGGBB. If it's 8 chars, we assume the last two are alpha.
    if (working_hex_string.length() == 8) {
        std::string alphaString = working_hex_string.substr(6, 2);
        unsigned long decimal_value = std::stoul(alphaString, nullptr, 16);
        alpha = static_cast<double>(decimal_value) / 255.0;
        working_hex_string.erase(6, 2); // Remove the last two characters (alpha)
    } else {
        alpha = 1.0;
    }

    // Check for the expected 6-character length
    if (working_hex_string.length() != 6) {
        std::cerr << "Warning: Invalid hex color string length: " << working_hex_string << " Setting color to default." << std::endl;
        return SOP_Read3mf::DEFAULT_H_COLOR;
    }

    // Convert hex string to integer
    // Use std::stoul (string to unsigned long) with base 16.
    unsigned long color_int;
    try {
        color_int = std::stoul(working_hex_string, nullptr, 16);
    }
    catch (const std::exception& e) {
        std::cerr << "Error: Failed to convert hex string to integer: " << e.what() << " Setting color to default." << std::endl;
        return SOP_Read3mf::DEFAULT_H_COLOR;
    }

    // Extract and normalize RGB components using bitwise operations

    // Red component: 0xRRGGBB -> (0xff0000 & color) >> 16
    float red = ((color_int & 0xFF0000) >> 16) / 255.0f;

    // Green component: 0xRRGGBB -> (0x00ff00 & color) >> 8
    float green = ((color_int & 0x00FF00) >> 8) / 255.0f;

    // Blue component: 0xRRGGBB -> 0x0000ff & color
    float blue = (color_int & 0x0000FF) / 255.0f;

    LOG_DEBUG(false, "Converted " << hex_string << " to RGB: " << red << ", " << green << ", " << blue);

    // Return the result as a UT_Vector3
    return UT_Vector3(red, green, blue);
}


//
// C++ equivalent of vex colormap() -- sample the color of a texture at a uv
// coordinate.
//
struct PixelColor {
    //unsigned char r, g, b, a;
    float r, g, b, a;
};
SOP_Read3mf::ErrorCode
colormap(std::string texturePath, const std::array<float, 2>& uv, PixelColor& color, bool debug) {
    LOG_DEBUG(debug, "Entering colormap.");

    int width, height, numChannels;

    unsigned char *data = stbi_load(texturePath.c_str(), &width, &height, &numChannels, 4);
    if (!data) {
        std::string errorMsg = "Error: Texture image file not found or failed to load: " + texturePath;
        std::cerr << errorMsg << std::endl;
        return SOP_Read3mf::ErrorCode::FILE_FAILURE;
    }
    // clamp values -- should I bother?
    int x = static_cast<int>(uv[0] * width);
    int y = static_cast<int>(uv[1] * height);
    x = std::max(0, std::min(x, width - 1));
    y = std::max(0, std::min(y, height - 1));

    size_t index = (size_t)y * width * 4 + (size_t)x * 4;

    color.r = data[index + 0] / 255.0f;
    color.g = data[index + 1] / 255.0f;
    color.b = data[index + 2] / 255.0f;
    color.a = data[index + 3] / 255.0f;

    stbi_image_free(data);
    LOG_DEBUG(debug, "    Got r, g, b, a of " + std::to_string(color.r) + ", " + std::to_string(color.g)
        + ", " + std::to_string(color.b) + ", " + std::to_string(color.a));

    LOG_DEBUG(debug, "Exiting colormap.");
    return SOP_Read3mf::ErrorCode::SUCCESS;
}


//
// Convert color to 3mf format hex string.
//
std::string
convertColorToHexString(PixelColor color, bool debug) {
    // Should I bother to check if the number is actually between 0 and 1?
    int rScaled = static_cast<int>(std::round(color.r * 255.0f));
    int gScaled = static_cast<int>(std::round(color.g * 255.0f));
    int bScaled = static_cast<int>(std::round(color.b * 255.0f));

    // convert to hex
    std::stringstream st;

    // Set the stream flags once:
    //   std::hex: output in hexadecimal format
    //   std::uppercase: use uppercase letters (optional, lower 'x' is default)
    //   std::setfill('0'): use '0' for padding
    st << "#" 
    << std::hex 
    << std::setfill('0');

    // Insert each scaled value with a width of 2
    st << std::setw(2) << rScaled
    << std::setw(2) << gScaled
    << std::setw(2) << bScaled;

    // Get the final string
    return st.str();
}


//
// Template function to print the contents of an unordered multimap dictionary.
// XXX Do we still need this?
//
template <typename K, typename V>
void
printMultiMap(const std::unordered_multimap<K, V>& map, const std::string& name, bool debug) {
    if (!debug) {
        return;
    }
    LOG_DEBUG(debug, std::endl);
    LOG_DEBUG(debug, "--- Contents of MultiMap: " + name + " ---");
    std::stringstream st;
    st << '{' << std::endl;
    for (const auto& pair : map) {
        // This relies on the operator << being defined for both K and V.
        st << "  Key: " << pair.first 
                  << " | Value: " << pair.second 
                  << std::endl;
    }
    st << '}' << std::endl;
    std::string output = st.str();
    LOG_DEBUG(debug, output);
}


// Use a single template parameter for the whole Map type
template <typename T>
void printMap(const T& map, const std::string& name, bool debug) {
    if (!debug) {
        return; // Optimization: don't even build the string if debug is off
    }
    LOG_DEBUG(debug, std::endl);
    LOG_DEBUG(debug, "--- Contents of Map: " + name + " ---");
    std::stringstream st;
    st << '{' << std::endl;
    
    // This loop works for both std::map and std::unordered_map
    // because they both yield "pairs" during iteration.
    for (const auto& pair : map) {
        st << "  Key: " << pair.first 
           << " | Value: " << pair.second 
           << std::endl;
    }
    
    st << '}' << std::endl;
    LOG_DEBUG(debug, st.str());
}


//
// Clear and reset class data structures.
//
SOP_Read3mf::ErrorCode
SOP_Read3mf::clearData() {
    LOG_DEBUG(this->debug, "Entering clearData.");

    for (auto it = this->objectDict.begin(); it != this->objectDict.end(); ++it) {
        // it->first is the int (ID)
        // it->second is the ObjectData*
        ObjectData* data = it->second; 
        if (data) {
            // Clear the handle so it releases the GU_Detail
            data->objGdpHandle.clear(); 
            
            // Delete the struct container I created with 'new'
            delete data;
        }
    }
    this->objectDict.clear();
    this->colorDict.clear();
    this->basematDict.clear();
    this->texture2dgroupDict.clear();
    this->multiPids.clear();
    this->vertexDict.clear();
    //this->pointDict.clear();
    this->textureModifyUVsDict.clear();
    this->textureFilesDict.clear();
    //this->texturePid2IdDict.clear();
    this->shaderDict.clear();
    this->buildDict.clear();

    this->scale_factor = 1.0f; // Read from 3mf file, so needs to be reset

    LOG_DEBUG(this->debug, "Exiting clearData");

    return ErrorCode::SUCCESS;
}


//
// Get the semantic pieces of the file name for a texture.
//
void
getFileInfo(std::string path, std::string &dir, std::string &stem, std::string &suffix) {
    LOG_DEBUG(false, "Entering getFileInfo.");
    
    // Set up pieces we'll use if we need to create a new texture file.
    LOG_DEBUG(false, "    Entering with path = " + path);

    // Create a std::filesystem::path object from the string
    std::filesystem::path full_path(path);

    // Split into path and filename
    std::filesystem::path dir_path = full_path.parent_path();
    std::filesystem::path file_name_with_ext = full_path.filename();

    // As std::string -- Do I need this?
    dir = dir_path.string();
    std::string file = file_name_with_ext.string();

    // Get the base name (stem)
    std::filesystem::path file_name_only = full_path.stem();

    // Get the extension (suffix, including the dot)
    std::filesystem::path suffix_with_dot = full_path.extension();

    // Convert the results back to std::string if needed
    stem = file_name_only.string();  // "my_texture"
    suffix = suffix_with_dot.string();  // ".png"

    LOG_DEBUG(false, "Exiting getFileInfo");

    return;
}


//
// Write out a new or revised texture file.
// Should I use jpg quality of 100 or leave it at 90? XXX
// Returns true for success.
//
bool
writeNewTexture(std::string dir, std::string stem, std::string suffix, std::string addition,
    int width, int height, int numChannels, unsigned char* imagePtr, int rowStride, std::string &newPath, bool debug) {

    LOG_DEBUG(debug, "Entering writeNewTexture with dir " + dir + " and addition " + addition + " width "
        + std::to_string(width) + " height " + std::to_string(height) + " rowStride " + std::to_string(rowStride));

    std::string newFilePath;    
    int success = 0;
    if (numChannels == 4) {
        if (strcmp(suffix.c_str(), ".png") != 0 && strcmp(suffix.c_str(), ".PNG") != 0) {
            std::cerr << "Warning: changing texture from " + suffix + " to .png to accommodate alpha channel." << std::endl;
        }
        // create name of new texture file
        newFilePath = dir + "/" + stem + addition + ".png";
        // Use stbi_write_png for RGBA (handles alpha)
        success = stbi_write_png(newFilePath.c_str(), width, height, numChannels, imagePtr, rowStride);
    } else {
        if (strcmp(suffix.c_str(), ".jpg") != 0 && strcmp(suffix.c_str(), ".JPG") != 0 && strcmp(suffix.c_str(), ".jpeg") != 0
            && strcmp(suffix.c_str(), ".JPEG") != 0) {
            std::cerr << "Warning: changing texture from " + suffix
                + " to .jpg since we lack an alpha channel and this gives us more compression." << std::endl;
        }
        // create name of new texture file
        newFilePath = dir + "/" + stem + addition + ".jpg";
        // Use stbi_write_jpg for RGB (if numChannels=3)
        success = stbi_write_jpg(newFilePath.c_str(), width, height, numChannels, imagePtr, 90); // 90 is jpg quality
    }

    if (!success) {
        std::cerr << "Error writing new image to: " << newFilePath << std::endl;
        return false;
    }
    newPath = newFilePath;

    LOG_DEBUG(debug, "Exiting writeNewTexture");

    return true;
}


//
// Convert texture to a 2-way tiling mirrored in both U and V and write it to
// a new texture file. In stbi, the top left corner of the image is (0,0). In Houdini, it is
// the bottom left corner that is (0,0). Because the memory storage order is inverted relative to the
// display system's Y-axis interpretation, the image appears upside down if mapped directly. So we
// put the original textfile image in the lower-left of the new textfile image, and this will then come
// out inverted and in the upper left on the display, which is what we want.
//
// Remember that a "vertical flip" means mirrored across the horizontal axis, and vice versa.
//
// Returns true for success.
//
bool
textureMirrorMirror(unsigned char* originalData, int width, int height, int numChannels,
    std::string dir, std::string stem, std::string suffix, std::string &usePath, bool debug) {

    LOG_DEBUG(debug, "Entering textureMirrorMirror with width " << width << " height " << height
        << " numChannels " << numChannels << " dir " << dir << " stem " << stem << " suffix " << suffix);

    // For new image buffer
    int newWidth = width * 2;
    int newHeight = height * 2;
    int pixelBytes = numChannels; // Bytes per pixel (3 for RGB, 4 for RGBA)
    int rowStrideOrig = width * pixelBytes;
    int rowStrideNew = newWidth * pixelBytes;

    // Allocate memory
    std::vector<unsigned char> newData(newWidth * newHeight * numChannels);

    // Ptr to start of new buffer
    unsigned char* newImagePtr = newData.data();

    for (int y = 0; y < height; ++y) {
        // Pointers for the row in the original image (originalRow) and its vertical flip (mirrorVRow).
        // We are working our way from top row to bottom row of the original image. For mirrorVRow, we
        // are working our way from the bottom row of the original image upwards.
        const unsigned char* originalRow = originalData + (y * rowStrideOrig);
        const unsigned char* mirrorVRow = originalData + ((height - 1 - y) * rowStrideOrig);

        // Pointers for the corresponding rows in the new 2x2 image -- where we'll place the rows
        // we're copying. So newRowTop is the place for originalRow to be copied to in the upper left
        // quadrant, and newBottomRow will be the row in the lower left quadrant. (We add height to y to
        // move it to the lower quadrant.)
        unsigned char* newRowTop = newImagePtr + (y * rowStrideNew);
        unsigned char* newRowBottom = newImagePtr + ((height + y) * rowStrideNew);

        // Paste original image in lower left
        // Copy original_row into the left half of the bottom section of the new image
        std::memcpy(newRowBottom, originalRow, rowStrideOrig);

        // Paste vertically mirrored image in upper left. 
        // Copy mirrorVRow (original image's V-flip) into the left half of the top section
        std::memcpy(newRowTop, mirrorVRow, rowStrideOrig);

        // Paste horizontally mirrored image in lower right
        // Manually copy the original row in reverse order (horizontal flip)
        unsigned char* q2Start = newRowBottom + rowStrideOrig;
        for (int x = 0; x < width; ++x) {
            std::memcpy(q2Start + (width - 1 - x) * pixelBytes, originalRow + x * pixelBytes, pixelBytes);
        }
        
        // Paste doubly mirrored image in upper right (doubly mirrored, v interted)
        // Manually copy the V-flipped row (mirrorVRow) in reverse order (horizontal flip)
        unsigned char* q4Start = newRowTop + rowStrideOrig;
        for (int x = 0; x < width; ++x) {
            std::memcpy(q4Start + (width - 1 - x) * pixelBytes, mirrorVRow + x * pixelBytes, pixelBytes);
        }
    }

    if (!writeNewTexture(dir, stem, suffix, "-mirroredUV", newWidth, newHeight, numChannels, newImagePtr, rowStrideNew,
        usePath, debug)) {
        std::cerr << "Error writing double-mirrored image to: " << usePath << std::endl;
        return false;
    }
    LOG_DEBUG(debug, "    Usepath is now " << usePath);

    return true;
}


//
// Convert texture to a 1-way tiling mirrored in U and write it to
// a new texture file
// Returns true for success.
//
bool
textureMirrorWrap(unsigned char* originalData, int width, int height, int numChannels,
    std::string dir, std::string stem, std::string suffix, std::string &usePath, bool debug) {
    // For new image buffer
    int newWidth = width * 2;
    int newHeight = height;
    int pixelBytes = numChannels; // Bytes per pixel (3 for RGB, 4 for RGBA)
    int rowStrideOrig = width * pixelBytes;
    int rowStrideNew = newWidth * pixelBytes;

    LOG_DEBUG(debug, "Entering textureMirrorWrap");
    // Allocate memory
    std::vector<unsigned char> newData(newWidth * newHeight * numChannels);

    // Ptr to start of new buffer
    unsigned char* newImagePtr = newData.data();

    // Do the mirroring. In Houdini v=0 is the bottom. In the new image v=0 is the top.

    for (int y = 0; y < height; ++y) {
        // Pointer for the current row in the original image (originalRow)
        const unsigned char* originalRow = originalData + (y * rowStrideOrig);

        // Pointer for the current row in the new 2x1 image
        unsigned char* newRow = newImagePtr + (y * rowStrideNew);

        // Paste original image on left
        // Copy original_row into the left half of the new row
        std::memcpy(newRow, originalRow, rowStrideOrig);

        // Paste horizontally mirrored image on right
        unsigned char* mirroredStart = newRow + rowStrideOrig;

        // Manually copy the pixels in reverse horizontal order
        for (int x = 0; x < width; ++x) {
            // Read pixel from originalRow at x, write to mirroredStart at (width - 1 - x)
            std::memcpy(mirroredStart + (width - 1 - x) * pixelBytes, originalRow + x * pixelBytes, pixelBytes);
        }
    }
    if (!writeNewTexture(dir, stem, suffix, "-mirroredU", newWidth, newHeight, numChannels, newImagePtr, rowStrideNew,
        usePath, debug)) {
        std::cerr << "Error writing U-mirrored image to: " << usePath << std::endl;
        return false;
    }
    LOG_DEBUG(debug, "Exiting textureMirrorWrap");

    return true;
}


//
// Convert texture to a 1-way tiling mirrored in V and write it to
// a new texture file
// Returns true for success.
//
bool
textureWrapMirror(unsigned char* originalData, int width, int height, int numChannels,
    std::string dir, std::string stem, std::string suffix, std::string &usePath, bool debug) {
    // For new image buffer
    int newWidth = width;
    int newHeight = height * 2;
    int pixelBytes = numChannels; // Bytes per pixel (3 for RGB, 4 for RGBA)
    int rowStride = width * pixelBytes;

    LOG_DEBUG(debug, "Entering textureWrapMirror");
    // Allocate memory
    std::vector<unsigned char> newData(newWidth * newHeight * numChannels);

    // Ptr to start of new buffer
    unsigned char* newImagePtr = newData.data();
    int vPasteStartRow = height;

    // Do the mirroring. In Houdini v=0 is the bottom. In the new image v=0 is the top.

    for (int y = 0; y < height; ++y) {
        // Pointer for the row in the original image (originalRow) used for bottom half
        const unsigned char* originalRow = originalData + (y * rowStride);

        // Vertically mirrored row: row (height - 1 - y) of the original image (used for top half)
        const unsigned char* mirrorVRow = originalData + ((height - 1 - y) * rowStride);

        // Paste original image on bottom (v=0 to v-1 range) which is height to newHeight -1 in the buffer.
        unsigned char* newRowBottom = newImagePtr + ((vPasteStartRow + y) * rowStride);
        std::memcpy(newRowBottom, originalRow, rowStride);

        // Paste vertically mirrored image on top
        unsigned char* newRowTop = newImagePtr + (y * rowStride);
        std::memcpy(newRowTop, mirrorVRow, rowStride);
    }

    if (!writeNewTexture(dir, stem, suffix, "-mirroredV", newWidth, newHeight, numChannels, newImagePtr, rowStride,
        usePath, debug)) {
        std::cerr << "Error writing V-mirrored image to: " << usePath << std::endl;
        return false;
    }
    LOG_DEBUG(debug, "Exiting textureWrapMirror");

    return true;
}


/*
 * --------------------------------------------------------------------------------
 * END OF UTILITY FUNCTIONS
 * --------------------------------------------------------------------------------
 */


//
// Constructor factory.
//
OP_Node *
SOP_Read3mf::myConstructor(OP_Network *net, const char *name, OP_Operator *op)
{
    return new SOP_Read3mf(net, name, op);
}

// Class constructor
SOP_Read3mf::SOP_Read3mf(OP_Network *net, const char *name, OP_Operator *op)
    : SOP_Node(net, name, op),  // Base class call
      loadGeometry(false)      // Initialize std::atomic memeber
{
    // This SOP does not manually manage its data IDs
    mySopFlags.setManagesDataIDs(false);
}


//
// Class destructor
//
SOP_Read3mf::~SOP_Read3mf() {
    clearData();
}


//
// Cooking SOP. The first time this is put down, nothing interesting happens. But a read callback will cause
// this sop to cook again, with a flag set to make it actually read in the 3mf model and create the geometry.
//
OP_ERROR
SOP_Read3mf::cookMySop(OP_Context &context)
{
    fpreal t = context.getTime();
    UT_Console::initConsole();

    LOG_DEBUG(this->debug, "Entering cookMySop");

    // Set up parameters. We at least need the debug for logging.
    this->debug = this->DEBUG(t);
    this->flip = this->FLIP(t);
    this->build = this->BUILD(t);
    this->timer = this->TIMER(t);
    this->FILENAME(this->filename, t);
    this->ASSETS(this->assets, t);

    this->start = generateTimestamp();
    this->t = t;

    LOG_DEBUG(false, "    Cook: We have " << this->gdp->getNumPoints() << " points.");

    // Atomically read the current state and set the flag to false (reset)
    // so we don't have two threads possibly trying to load the geometry
    // at the same time. That might be okay but sounds inefficient.
    bool shouldLoad = this->loadGeometry.exchange(false); 

    if (!shouldLoad) {
        // If we shouldn't load a geometry, the function exits, returning the cached geometry.
        LOG_DEBUG(this->debug, "Exiting cookMySop with no geometry to cook");
        return OP_ERROR::UT_ERROR_NONE;
    }

    LOG_DEBUG(false, "Should load is " << shouldLoad);
    LOG_DEBUG(false, "We need to load the 3mf file and build the geometry.");

    // Clear/reset data structures since the last time we loaded the geometry
    clearData();
    // Since we have no input, manually clear and prepare the output detail
    this->gdp->appendPoint();
    this->gdp->clearAndDestroy();

    LOG_DEBUG(false, "    After destroy, we have " << this->gdp->getNumPoints() << " points.");

    // Set up our internal error reporting -- will phase this out and shift to Houdini's instead XXX
    SOP_Read3mf::ErrorCode myError = SOP_Read3mf::ErrorCode::SUCCESS;

    // Get the name of the extract folder and create it.
    if (this->assets.empty()) {
        std::cerr << "Error: The name of the folder for unpacking the 3mf file is empty." << std::endl;
        //this->addMessage(SOP_MESSAGE, "Foodle!");
        //this->addError(SOP_ERR_FILEGEO, "Error: The name of the folder for unpacking the 3mf file is empty.");
        this->addError(UT_ERROR_ABORT, "Error: The name of the folder for unpacking the 3mf file is empty.");
        LOG_DEBUG(true, "Error is " << this->error());

        return this->error();
    }

    // We append the node name to make sure the folder name is unique inside this session.
    this->extractFolder = this->assets + "/" + this->getName().buffer();
    LOG_DEBUG(this->debug, "Extract folder is " << this->extractFolder);

     // Get 3mf file to import
    if (this->filename.empty()) {
        std::cerr << "Error: The filename for import is empty." << std::endl;
        //this->addError(SOP_MESSAGE, "Error: The filename for import is empty.");
        this->addError(SOP_ERR_FILEGEO, "Error: The filename for import is empty.");
        LOG_DEBUG(true, "Error is " << this->error() << " and UT_ERROR_ABORT is " << UT_ERROR_ABORT);
        return error();
    }
    LOG_DEBUG(this->debug, "The filename for import is " << this->filename);

    std::string suffix = ".3mf";
    if (this->filename.length() < suffix.length() || (this->filename.rfind(suffix) != this->filename.length() - suffix.length())) {
        this->filename += suffix;
    }

    // Unpack the 3mf archive
    if (unzipArchive(this->filename, this->extractFolder) != ErrorCode::SUCCESS) {
        std::cerr << "Error: Failed to unzip 3mf archive." << std::endl;
        this->addError(SOP_MESSAGE, "Error: Failed to unzip the 3mf archive.");
        return error();
    }
    LOG_DEBUG(false, "Finished unzipping archive.");

    // Get the name of the model file.
    std::string rels_path;
    try {
        rels_path = (std::filesystem::path(this->extractFolder) / "_rels/.rels").string();
    } catch (const std::exception& e) {
        std::cerr << "Error: Path construction failed for the .rels file in the 3mf archive." << std::endl;
        this->addError(SOP_MESSAGE, "Error: Path construction failed for the .rels file in the 3mf archive.");
        return error();
    }
    LOG_DEBUG(this->debug, "The rels path is " << rels_path);

    std::string model_file;
    if (this->getModelFile(rels_path, model_file) != ErrorCode::SUCCESS) {
        std::cerr << "Error: No usable model file name in the 3mf archive." << std::endl;
        this->addError(SOP_MESSAGE, "Error: No usable model file name in the 3mf archive.");
        return error();
    }

    std::filesystem::path the_model_path = std::filesystem::path(this->extractFolder) / model_file;
    std::string the_model = the_model_path.string();
    LOG_DEBUG(this->debug, "We have the model file including path as " << the_model);

    if (this->parseModel(the_model) != ErrorCode::SUCCESS) {
        std::cerr << "Unable to parse the 3mf model file." << std::endl;
        this->addError(SOP_MESSAGE, "Unable to parse the 3mf model file.");
        return error();
    }

    auto endTime = generateTimestamp();
    auto durationTime = endTime - this->start;
    auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(durationTime);
    long long total_ms = duration_ms.count();
    long long mins  = (total_ms / 60000);
    long long secs  = (total_ms % 60000) / 1000;
    long long msecs = (total_ms % 1000);

    LOG_DEBUG(this->debug, "\nCompleted cooking with no errors for 3mf file " << this->filename);
    UT_String timeMsg;
    timeMsg.sprintf("Elapsed Time: %02d:%02d:%03d (m:s:ms)", mins, secs, msecs);
    LOG_DEBUG(this->timer || this->debug, timeMsg);

    // Add time as information on the node
    if (this->timer) {
        addMessage(SOP_MESSAGE, timeMsg.buffer());
    }

    return error();
}


//
// This is the callback from a button push to read in a 3mf model from the file system.
//
// Note that a callback requires a return value of 0 to indicate failure and 1 for success.
//
// In this callback we first create the nodes we'll need for textures.
// Then we set flags to cause cookMySop to run and to recognize that this time it
// actually has to do something (i.e. read in the 3mf file and build the geometry). So the division of
// labor is that the node setup happens here and the geometry setup happens in cookMySop()
//
/*static*/ int
SOP_Read3mf::read(void *data, int index, fpreal t, const PRM_Template *tplate)
{
    // Get the node for this instance of the callback
    SOP_Read3mf *this_node = static_cast<SOP_Read3mf*>(data);

    if (!this_node) {
        std::cerr << "Error: Unable to retrieve node for this callback" << std::endl;
        return 0;
    }

    LOG_DEBUG(this_node->debug, "Entering read");

    // The first time through we need to create the machinery to use the textures.
    // We put all of this inside a untility subnet.
    // We create the subnet with name from our node name, which will make it
    // unique if another 3mf reader node has also been instantiated.
    // After creating the utility subnet, we put down a matnet in it.
    // The we put down a shader inside the matnet and set the path and other parameters.

    // Get the absolute path of the current SOP
    UT_String ourFullPath; // Full path to our node
    this_node->getFullPath(ourFullPath);
    LOG_DEBUG(this_node->debug, "    Full path to our node is " << ourFullPath);
    UT_String parentPath, ourName;
    ourFullPath.splitPath(parentPath, ourName); // Split fullPath into parent path and our node name

    // Tidy up the concatenation using std::string. It seems .buffer() is the HDK way to get the const char*
    std::string ourNameStr(ourName.buffer());
    std::string parentPathStr(parentPath.buffer());

    // Since our node must have a unique name, this ensures the subnet does too
    std::string subnetNameStr = ourNameStr + "_subnet";
    LOG_DEBUG(this_node->debug, "    Unique name for subnet node: " << subnetNameStr);

    // Ensure the path doesn't end up with double slashes if parent is "/"
    std::string fullSubnetPathStr;
    if (parentPathStr == "/") {
        fullSubnetPathStr = "/" + subnetNameStr;
    } else {
        fullSubnetPathStr = parentPathStr + "/" + subnetNameStr;
    }

    // Assign back to UT_String members -- subnetPath is just for debugging really
    this_node->subnetPath = fullSubnetPathStr.c_str();

    LOG_DEBUG(this_node->debug, "    Parent: " << parentPath);
    LOG_DEBUG(this_node->debug, "    Subnet Path: " << this_node->subnetPath);
    LOG_DEBUG(false, "    utility subnet path is " << this_node->subnetPath);

    std::string script;
    script += "import hou\n";

    // Find or create the parent and subnet
    script += "parent = hou.node('" + parentPathStr + "')\n";
    script += "subnet = parent.node('" + subnetNameStr + "') or parent.createNode('subnet', '" + subnetNameStr + "')\n";

    // Find or create the material network
    script += "matnet = subnet.node('3mf_materials') or subnet.createNode('matnet', '3mf_materials')\n";

    script += "if matnet:\n";
    // Find or Create the shader
    script += "    shader = matnet.node('master_3mf_shader') or matnet.createNode('principledshader', 'master_3mf_shader')\n";
    script += "    \n";
    // Set parameters for the material texture override
    script += "    shader.setParms({\n";
    script += "        'basecolor_useTexture': 1,\n"; // Enables the texture slot
    script += "        'basecolorr': 1.0,\n";        // White base color (1.0 multiplier)
    script += "        'basecolorg': 1.0,\n";
    script += "        'basecolorb': 1.0\n";
    script += "    })\n";
    script += "    \n";
    // Visual polish and a note for the user
    script += "    shader.setColor(hou.Color((0.0, 0.4, 0.4)))\n"; // Teal node color
    script += "    shader.setComment('3MF Master Shader: Driven by material_override attribute.')\n";
    script += "    shader.setGenericFlag(hou.nodeFlag.DisplayComment, True)\n";
    script += "    \n";
    // Organization and viewport nudge -- does that matter? XXX
    script += "    matnet.layoutChildren()\n";
    script += "    hou.hscript('glcache -m 5000')\n";

    // Execute the final script
    PYrunPythonStatements(script.c_str());

    this_node->shaderNode = fullSubnetPathStr + "/3mf_materials/master_3mf_shader";
    // Find the node
    OP_Node* shaderNode = OPgetDirector()->findNode(this_node->shaderNode);
    if (!shaderNode) {
        // This is the only way I can figure out to show there was an error in the callback itself.
        PYrunPythonStatements("import hou\nhou.ui.displayMessage('Failed to create shader node for potential textures.', severity=hou.severityType.Error)");
    }
    LOG_DEBUG(this_node->debug, "    Set shaderNode path to " << this_node->shaderNode);

    // Now read in the 3mf file and create the geometry
    this_node->loadGeometry = true; // So cookMySop knows to read the 3mf file
    this_node->forceRecook(); // Cause node to cook again
    LOG_DEBUG(this_node->debug, "Load geometry is " << this_node->loadGeometry);
    LOG_DEBUG(this_node->debug, "Our gdp is currently null? " << !this_node->gdp);

    LOG_DEBUG(this_node->debug, "Exiting read");
    return 1;
}


//
// Get the name of the model file from the relationship file in the 3mf archive.
//
SOP_Read3mf::ErrorCode
SOP_Read3mf::getModelFile(std::string rels_path, std::string& model_file) {

    LOG_DEBUG(this->debug, "Entering getModelFile");

    XMLDocument rels_doc;

    if (rels_doc.LoadFile(rels_path.c_str()) != XML_SUCCESS) {
        std::cerr << "Error: Could not parse XML relations file " << rels_path << ": "
            << rels_doc.ErrorIDToName(rels_doc.ErrorID());
        return ErrorCode::XML_FAILURE;
    }

    // Get the root element
    XMLElement* rels_root = rels_doc.RootElement();
    if (!rels_root) {
        std::cerr << "Error: rels file is empty or missing a root element." << std::endl;
        return ErrorCode::XML_FAILURE;
    }

    // Iterate through all <Relationship> elements
    for (XMLElement* relationship_elem = rels_root->FirstChildElement("Relationship");
         relationship_elem != nullptr;
         relationship_elem = relationship_elem->NextSiblingElement("Relationship")) {
        
        // Get attributes
        const char* target_value_c = relationship_elem->Attribute("Target");
        const char* type_value_c = relationship_elem->Attribute("Type");
        
        if (!target_value_c || !type_value_c) {
            continue;
        }
        
        std::string target_value = target_value_c;
        std::string type_value = type_value_c;

        // Log the values
        LOG_DEBUG(false, "  Target: " << target_value);
        LOG_DEBUG(false, "  Type: " << type_value);
        
        // Check the type: if type_value and type_value.endswith("3dmodel")
        if (type_value.size() >= 7 && type_value.substr(type_value.size() - 7) == "3dmodel") {
            
            if (!target_value.empty()) {
                // Remove leading '/' if present
                if (target_value.front() == '/') {
                    model_file = target_value.substr(1);
                } else {
                    model_file = target_value;
                }
                break;
            }
        }
    }

    LOG_DEBUG(this->debug, "We have the model file " << model_file);
    LOG_DEBUG(this->debug, "Exiting getModelFile");

    return ErrorCode::SUCCESS;
}


//
// Parse the 3mf model to build the Houdini geometry.
//
SOP_Read3mf::ErrorCode
SOP_Read3mf::parseModel(std::string the_model) {

    LOG_DEBUG(this->debug, "Entering parseModel");
    try {
        // Load and Parse the XML
        XMLDocument model_doc;
        XMLError result = model_doc.LoadFile(the_model.c_str());
        if (result != XML_SUCCESS) {
            std::cerr << "Error: Couldn't parse 3mf XML model file: " << model_doc.ErrorIDToName(result) << std::endl;
            return ErrorCode::XML_FAILURE;
        }

        XMLElement* root = model_doc.RootElement();
        if (!root) {
             std::cerr << "Error: 3mf model file in XML file has no root element." << std::endl;
             return ErrorCode::XML_FAILURE;
        }
        
        // Root Element Tag
        std::string root_tag = root->Name(); 
        LOG_DEBUG(false, "Root Element Tag: " << root_tag);

        // Unit Conversion and Scaling
        const char* unit_value_c = root->Attribute("unit");
        
        if (unit_value_c) {
            std::string attr_value = unit_value_c;

            if (attr_value != "millimeter") {
                LOG_DEBUG(this->debug, "This model is not using the default units of millimeters, but instead "
                    << attr_value << ". I will scale it to millimeters.");
                
                if (attr_value == "micron") {
                    this->scale_factor = 0.001f;
                } else if (attr_value == "centimeter") {
                    this->scale_factor = 10.0f;
                } else if (attr_value == "inch") {
                    this->scale_factor = 25.4f;
                } else if (attr_value == "foot") {
                    this->scale_factor = 304.8f;
                } else if (attr_value == "meter") {
                    this->scale_factor = 1000.0f;
                } else {
                    std::cerr << "Error: " << attr_value << " is not a valid unit setting for 3mf." << std::endl;
                    return ErrorCode::BAD_3MF;
                }
            }
        }

        // Parse Top-Level Child Nodes
        int num_resources = 0;
        int num_builds = 0;
        int num_metadata = 0;
       
        for (XMLElement* descendant = root->FirstChildElement();
             descendant != nullptr;
             descendant = descendant->NextSiblingElement()) {
            
            std::string tag_name = descendant->Name(); 
            LOG_DEBUG(false, "We got tagname " << tag_name);
            
            if (tag_name == "resources") {
                num_resources++;
                if (this->handleResources(descendant) != ErrorCode::SUCCESS) {
                    std::cerr << "Error: Failed to handle resources section of the 3mf file." << std::endl;
                    return ErrorCode::BAD_3MF;
                }
            } else if (tag_name == "metadata") {
                num_metadata++;
                LOG_DEBUG(this->debug, "Warning: We do not yet handle model metadata.");
            } else if (tag_name == "build") {
                if (this->build) {
                    if (this->handleBuild(descendant) != ErrorCode::SUCCESS) {
                        std::cerr << "Error: Failed to handle build section of the 3mf file." << std::endl;
                        return ErrorCode::BAD_3MF;
                    }
                } 
                num_builds++;
            } else {
                std::cerr << "Warning: We do not yet handle the " << tag_name << " 3mf tag." << std::endl;
            }
        }       

        if (num_resources > 1) {
            std::cerr << "Error: There should only be 1 resource section in a model file, but there were "
                << num_resources << std::endl;
            return ErrorCode::BAD_3MF;
        }

        if (num_builds > 1) {
            std::cerr << "Error: There should only be 1 build section in a model file, but there were "
                << num_builds << std::endl;
            return ErrorCode::BAD_3MF;
        }
    } catch (...) {
        std::cerr << "Error: An unknown, unexpected error occurred." << std::endl;
        return ErrorCode::BAD_3MF;
    }

    // Process build list and merge into GDP
    UT_Matrix4 scale_matrix(this->scale_factor);
    this->gdp->clearAndDestroy();
    for (auto const& [buildID, transformList] : buildDict) {
        auto ito = objectDict.find(buildID);
        if (ito == objectDict.end() || !ito->second->objGdpHandle.isValid()) {
            std::cerr << "Error: Object ID " << buildID << " has no geometry." << std::endl;
            continue;
        }
        LOG_DEBUG(this->debug, "    Going through buildList we have object " << buildID);

        const GU_Detail* sourceGdp = ito->second->objGdpHandle.gdp();
        if (!sourceGdp) {
            std::cerr << "Error: No geometry detail for an object." << std::endl;
            return ErrorCode::OTHER;
        }

        for (const UT_Matrix4& mat : transformList) {
            GA_Index startIdx = this->gdp->getNumPoints();
            this->gdp->merge(*sourceGdp);
            GA_Index endIdx = this->gdp->getNumPoints();

            if (endIdx > startIdx) {
                UT_Matrix4 finalMat(mat);
                finalMat *= scale_matrix;

                GA_Range newPoints(this->gdp->getPointMap(), startIdx, endIdx);

                this->gdp->transform(
                    finalMat, 
                    GA_Range(), 
                    newPoints,   
                    false        
                );
            }
        }
    }

    printMap(buildDict, "buildDict", this->debug);
    printMap(vertexDict, "vertexDict", this->debug);
    printMap(objectDict, "objectDict", this->debug);
    printMap(texture2dgroupDict, "texture2dgroupDict", this->debug);
    printMap(shaderDict, "shaderDict", this->debug);
    printMap(textureFilesDict, "textureFilesDict", this->debug);

    LOG_DEBUG(this->debug, "Exiting parseModel");
    return ErrorCode::SUCCESS;
}


//
// Parse the resources part of the tree and set up Houdini data structures.
//
SOP_Read3mf::ErrorCode
SOP_Read3mf::handleResources(XMLElement* element) {

    LOG_DEBUG(this->debug, "Entering handleResources");
    ErrorCode err = ErrorCode::SUCCESS;

    // Get the first child element of 'element'
    tinyxml2::XMLElement* descendant = element->FirstChildElement();
    while (descendant != nullptr) {
        const char* tag_name_cstr = descendant->Name(); // plain tag name
        std::string tag_name(tag_name_cstr ? tag_name_cstr : ""); // Safely get tag name
        if (tag_name == "object") {
            err = handleObject(descendant);
        } else if (tag_name == "m:colorgroup") {
            err = handleColorgroup(descendant);
        } else if (tag_name == "m:texture2d") {
            err = handleTexture2d(descendant);
        } else if (tag_name == "m:texture2dgroup") {
            err = handleTexture2dgroup(descendant);
        } else if (tag_name == "basematerials") {
            err = handleBasematerials(descendant);
        } else if (tag_name == "m:multiproperties") {
            err = handleMultiproperties(descendant);
        } else if (tag_name == "m:compositematerials") {
            std::cerr << "Warning: We do not yet handle composite materials, so results might be suspect." << std::endl;
        } else {
            // Use std::string for warning message formatting
            std::cerr << "[Warning: We do not yet handle the resource " << tag_name << ", so results might be suspect." << std::endl;
        }

        if (err != ErrorCode::SUCCESS) {
            return err;
        }
        descendant = descendant->NextSiblingElement();
    }

    LOG_DEBUG(this->debug, "Exiting handleResources");

    return ErrorCode::SUCCESS;
}


//
// Parse the entry that sets up a colorgroup.
//
SOP_Read3mf::ErrorCode
SOP_Read3mf::handleColorgroup(XMLElement* element) {

    LOG_DEBUG(this->debug, "Entering handleColorgroup");

    std::vector<std::string> colorArray;
    int id;
    
    tinyxml2::XMLError result = element->QueryIntAttribute("id", &id);

    if (result != tinyxml2::XML_SUCCESS) {
        addError(UT_ERROR_FATAL, "Error: Colorgroup element is missing an 'id' or has a non-integer 'id' attribute.");
        std::cerr << "Error: Colorgroup element is missing an 'id' or has a non-integer 'id' attribute." << std::endl;
        return SOP_Read3mf::ErrorCode::BAD_3MF; 
    }
    if (colorDict.count(id)) {
        LOG_DEBUG(this->debug, "Got colorgroup id " << id << " again.");
        addError(UT_ERROR_FATAL, "Error: Repeated colorgroup id found.");
        return SOP_Read3mf::ErrorCode::BAD_3MF;
    }
    
    for (tinyxml2::XMLElement* descendant = element->FirstChildElement(); 
        descendant != nullptr; 
        descendant = descendant->NextSiblingElement()) {
        
        LOG_DEBUG(false, "    testing element");

        if (strcmp(descendant->Name(), "m:color") == 0) {
            std::string color;
            ErrorCode err = handleColor(descendant, color);
            if (err != ErrorCode::SUCCESS) {
                std::cerr << "Error: Could not get color for colorgroup." << std::endl;
                return ErrorCode::BAD_3MF;
            }
            LOG_DEBUG(false, "Color returned is " << color);
            colorArray.push_back(color);
        }
    }
    for (int i = 0; i < colorArray.size(); i++) {
        LOG_DEBUG(false, "Color Array of " << i << " is " << colorArray[i]);
    }
    
    colorDict[id] = colorArray;

    LOG_DEBUG(this->debug, "Exiting handleColorgroup");

    return ErrorCode::SUCCESS;
}


//
// Parse the entry that sets up base materials.
//
SOP_Read3mf::ErrorCode
SOP_Read3mf::handleBasematerials(XMLElement* element) {

    LOG_DEBUG(this->debug, "Entering handleBasematerial");

    std::vector<std::string> colorArray;
    int id;
    
    tinyxml2::XMLError result = element->QueryIntAttribute("id", &id);

    if (result != tinyxml2::XML_SUCCESS) {
        addError(UT_ERROR_FATAL, "Error: basematerial element is missing an 'id' or has a non-integer 'id' attribute.");
        std::cerr << "Error: basematerial element is missing an 'id' or has a non-integer 'id' attribute." << std::endl;
        return SOP_Read3mf::ErrorCode::BAD_3MF; 
    }
    if (basematDict.count(id)) {
        LOG_DEBUG(this->debug, "Got basematerial id " << id << " again.");
        addError(UT_ERROR_FATAL, "Error: Repeated basematerial id found.");
        return SOP_Read3mf::ErrorCode::BAD_3MF;
    }
    
    for (tinyxml2::XMLElement* descendant = element->FirstChildElement(); 
        descendant != nullptr; 
        descendant = descendant->NextSiblingElement()) {
        
        LOG_DEBUG(false, "    testing element");

        if (strcmp(descendant->Name(), "base") == 0) {
            std::string color;
            ErrorCode err = handleBase(descendant, color);
            if (err != ErrorCode::SUCCESS) {
                std::cerr << "Error: Could not get base material color for base materials." << std::endl;
                return ErrorCode::BAD_3MF;
            }
            LOG_DEBUG(false, "Base color returned is " << color);
            colorArray.push_back(color);
        }
    }
    for (int i = 0; i < colorArray.size(); i++) {
        LOG_DEBUG(false, "Base color Array of " << i << " is " << colorArray[i]);
    }
    
    basematDict[id] = colorArray;

    LOG_DEBUG(this->debug, "Exiting handleBasematerials");

    return ErrorCode::SUCCESS;
}


//
// Parse the color and return it.
//
SOP_Read3mf::ErrorCode
SOP_Read3mf::handleColor(XMLElement* element, std::string& color) {

    LOG_DEBUG(false, "Entering handleColor");

    const char* color_attr = element->Attribute("color");

    if (!color_attr) {
        LOG_DEBUG(this->debug, "Error: A color has no information.");
        return SOP_Read3mf::ErrorCode::BAD_3MF; 
    }

    // If the attribute exists, copy the C-string value into the std::string reference.
    color = color_attr;

    LOG_DEBUG(false, "Exiting handleColor");

    return ErrorCode::SUCCESS;
}


//
// Parse the basematerial color and return it.
//
SOP_Read3mf::ErrorCode
SOP_Read3mf::handleBase(XMLElement* element, std::string& color) {

    LOG_DEBUG(false, "Entering handleBase");

    const char* color_attr = element->Attribute("displaycolor");

    if (!color_attr) {
        LOG_DEBUG(this->debug, "Error: A base color (displaycolor) has no information.");
        return SOP_Read3mf::ErrorCode::BAD_3MF; 
    }

    // If the attribute exists, copy the C-string value into the std::string reference.
    color = color_attr;

    LOG_DEBUG(false, "Exiting handleBase");

    return ErrorCode::SUCCESS;
}


//
// Locate and set up the required texture. Create matnet nodes
// and material nodes as needed.
//
SOP_Read3mf::ErrorCode
SOP_Read3mf::handleTexture2d(tinyxml2::XMLElement* element) {

    LOG_DEBUG(this->debug, "Entering handleTexture2d.");

    int id;
    tinyxml2::XMLError result = element->QueryIntAttribute("id", &id);
    if (result != tinyxml2::XML_SUCCESS) {
        addError(UT_ERROR_FATAL, "Error: Texture2d element is missing an 'id' or has a non-integer 'id' attribute.");
        std::cerr << "Error: Texture2d element is missing an 'id' or has a non-integer 'id' attribute." << std::endl;
        return SOP_Read3mf::ErrorCode::BAD_3MF; 
    }

    const char* contenttypeC; 
    std::string contenttype;
    result = element->QueryStringAttribute("contenttype", &contenttypeC);
    if (result != tinyxml2::XML_SUCCESS) {
        addError(UT_ERROR_FATAL, "Error: A texture failed to have a contenttype.");
        std::cerr << "Error: A texture failed to have a contenttype." << std::endl;
        return SOP_Read3mf::ErrorCode::BAD_3MF; 
    } else {
        contenttype = contenttypeC;
    }

    const char* pathC;
    std::string path;
    result = element->QueryStringAttribute("path", &pathC);
    if (result != tinyxml2::XML_SUCCESS) {
        addError(UT_ERROR_FATAL, "Error: A file-based texture failed to have a path.");
        std::cerr << "Error: A file-based texture failed to have a path." << std::endl;
        return SOP_Read3mf::ErrorCode::BAD_3MF; 
    } else {
        path = pathC;
        LOG_DEBUG(this->debug, "Texture path is " << path);
    }

    const char* filterC;
    Filters filter = Filters::AUTO; //default
    result = element->QueryStringAttribute("filter", &filterC);
    if (result == tinyxml2::XML_SUCCESS) {
        addError(UT_ERROR_FATAL, "Warning: We do not yet handle filters on textures -- setting to default.");
        std::cerr << "Warning: We do not yet handle filters on textures -- setting to default." << std::endl;
    }

    Tiling tilestyleu = Tiling::WRAP;  // set defaults
    Tiling tilestylev = Tiling::WRAP;

    const char* tileUC;
    result = element->QueryStringAttribute("tilestyleu", &tileUC);
    if (result != tinyxml2::XML_SUCCESS) {
        LOG_DEBUG(false, "Warning: No tiling information in the u direction -- will set to default 'wrap'.");
    } else {
        LOG_DEBUG(this->debug, "We have " << tileUC << " style texture tiling in U.");
        if (std::strcmp(tileUC, "wrap") == 0) {
           ; // Already set as default 
        } else if (std::strcmp(tileUC, "mirror") == 0) {
            tilestyleu = Tiling::MIRROR;
        } else if (std::strcmp(tileUC, "none") == 0) {
            tilestyleu = Tiling::NONE;
        } else if (std::strcmp(tileUC,"clamp") == 0) {
            tilestyleu = Tiling::CLAMP;
        } else {
            // Note: For addError, we can't easily use << unless your macro supports it, 
            // so I'm using a temporary stringstream here to keep the code clean.
            std::stringstream ss;
            ss << "Warning: Type of requested tiling (" << tileUC << ") in u is unknown. Will default to wrap.";
            addError(UT_ERROR_FATAL, ss.str().c_str());
            std::cerr << ss.str() << std::endl;
        }
    }

    const char* tileVC;
    result = element->QueryStringAttribute("tilestylev", &tileVC);
    if (result != tinyxml2::XML_SUCCESS) {
        LOG_DEBUG(false, "Warning: No tiling information in the v direction -- will set to default 'wrap'.");
    } else {
        LOG_DEBUG(this->debug, "We have " << tileVC << " style texture tiling in V.");
        if (std::strcmp(tileVC, "wrap") == 0) {
           ; // Already set as default 
        } else if (std::strcmp(tileVC, "mirror") == 0) {
            tilestylev = Tiling::MIRROR;
        } else if (std::strcmp(tileVC, "none") == 0) {
            tilestylev = Tiling::NONE;
        } else if (std::strcmp(tileVC, "clamp") == 0){
            tilestylev = Tiling::CLAMP;
        } else {
            std::stringstream ss;
            ss << "Warning: Type of requested tiling (" << tileVC << ") in v is unknown. Will default to wrap.";
            addError(UT_ERROR_FATAL, ss.str().c_str());
            std::cerr << ss.str() << std::endl;
        }
    }

    // Deal with tiling.
    path = this->extractFolder + path;
    std::string usePath;
    if (handleTiling(id, path, tilestyleu, tilestylev, usePath) != ErrorCode::SUCCESS) {
        std::cerr << "Error: Unable to handle tiling of texture " << usePath << std::endl;
        return ErrorCode::OTHER;
    }
    textureFilesDict[id] = usePath;

    LOG_DEBUG(this->debug, "Exiting handleTexture2d.");

    return ErrorCode::SUCCESS;
}


//
// Modify texture image depending on the type of tiling required.
// Set up how we'll need to modify uv's as we read them in later. Alas, here we
// don't yet know the max uv coordinates, so if the tile style is clamp or none,
// we might have to re-edit the texture we put together here.
//
SOP_Read3mf::ErrorCode
SOP_Read3mf::handleTiling(int id, std::string path, Tiling tilestyleU, Tiling tilestyleV, std::string& usePath) {

    LOG_DEBUG(this->debug, "Entering handleTiling");
    LOG_DEBUG(false, "    We came in with path of " << path);

    std::string dir;
    std::string stem;
    std::string suffix;
    usePath = path;
    
    getFileInfo(usePath, dir, stem, suffix);
    
    int width, height, numChannels;
    // Load image from file
    // Return value is a pointer to the start of the pixel data
    unsigned char* originalData = stbi_load(
        usePath.c_str(),// file path
        &width,         // address for storing width
        &height,        // address for storing height
        &numChannels,  // address for string number of channels (3 for RGB, 4 for RGBA)
        0               // desired # of channels (0 means the file's channels, 4 means force RGBA)
    );

    if (!originalData) {
        std::cerr << "Error: Unable to open texture file " << path << std::endl;
        return ErrorCode::FILE_FAILURE;
    }

    // Default values for scale to start with
    textureModifyUVsDict.emplace(id, TextureInfo(1.0, 1.0, tilestyleU, tilestyleV));

    // XXX
    // I'm setting clamp tiling to none here. They are mostly the same except for none doing something
    // different about alpha=0 except on the first layer. So I treat them the same way in the rest of
    // the code. I'll have to come back and differentiate them.
    //
    bool success = true; // Default tiling is wrap, which should pass through this function with success
    if (tilestyleU == Tiling::CLAMP) {
        tilestyleU = Tiling::NONE;
    } else if (tilestyleV == Tiling::CLAMP) {
        tilestyleV = Tiling::NONE;
    }
    // For Wrap/Wrap we do nothing.
    if (tilestyleU == Tiling::MIRROR && tilestyleV == Tiling::MIRROR) {
        success = textureMirrorMirror(originalData, width, height, numChannels, dir, stem, suffix, usePath, this->debug);
        textureModifyUVsDict[id] = {0.5f, 0.5f, tilestyleU, tilestyleV};
    } else if (tilestyleU == Tiling::MIRROR && tilestyleV == Tiling::WRAP) {
        success = textureMirrorWrap(originalData, width, height, numChannels, dir, stem, suffix, usePath, this->debug);
        textureModifyUVsDict[id] = {0.5f, 1.0f, tilestyleU, tilestyleV};
    } else if (tilestyleU == Tiling::WRAP && tilestyleV == Tiling::MIRROR) {
        success = textureWrapMirror(originalData, width, height, numChannels, dir, stem, suffix, usePath, this->debug);
        textureModifyUVsDict[id] = {1.0f, 0.5f, tilestyleU, tilestyleV};
    } else if (tilestyleU == Tiling::MIRROR && tilestyleV == Tiling::NONE) {
        LOG_DEBUG(this->debug, "    FOO in U");
        // Just a simple mirror in U and we wait to do the streaking for NONE until later
        success = textureMirrorWrap(originalData, width, height, numChannels, dir, stem, suffix, usePath, this->debug);
        textureModifyUVsDict[id] = {0.5f, 1.0f, tilestyleU, tilestyleV};
    } else if (tilestyleV == Tiling::MIRROR && tilestyleU == Tiling::NONE) {
        LOG_DEBUG(this->debug, "    FOO in V");
        // Just a simple mirror in V and we wait to do the streaking for NONE until later
        success = textureWrapMirror(originalData, width, height, numChannels, dir, stem, suffix, usePath, this->debug);
        textureModifyUVsDict[id] = {1.0f, 0.5f, tilestyleU, tilestyleV};
    }
    // Note that we do not deal with clamp/none tiling here, since we need to know the max and min uv coordinates
    // before we can do that. That happens in handleTexture2dgroup() instead.
    LOG_DEBUG(false, "    Coming back from tiling, usePath is now " + usePath);

    stbi_image_free(originalData);
    LOG_DEBUG(this->debug, "Exiting handleTiling with success of " + std::to_string(success));
    if (!success) {
        return ErrorCode::OTHER;
    }
    return ErrorCode::SUCCESS;
}


//
// Parse the texture2dgroup and set up uv info for the texture.
// Since we don't know the max uv coordinates until this point, we might have to
// create a new texture if the tiling style is clamp or none. This is a total pain, but
// I'm not sure what else to do, since when we handle the tiling originally, we don't know
// the max coordinates, so there's no point in creating the new texture there.
//
SOP_Read3mf::ErrorCode
SOP_Read3mf::handleTexture2dgroup(tinyxml2::XMLElement* element) {                

    LOG_DEBUG(this->debug, "Entering handleTexture2dgroup.");

    const char* id_cstr = element->Attribute("id");
    if (id_cstr == nullptr) {
        std::cerr << "Error: A texture2dgroup has no id." << std::endl;
        return ErrorCode::BAD_3MF;
    }
    const int id = std::stoi(id_cstr);
    const char* texid_cstr = element->Attribute("texid");
    if (texid_cstr == nullptr) {
        std::cerr << "Error: A texture2dgroup has no texid." << std::endl;
        return ErrorCode::BAD_3MF;
    }
    const int texid = std::stoi(texid_cstr);

    // Get the texture file for this texid
    auto itf = textureFilesDict.find(texid);
    if (itf == textureFilesDict.end()) {
        std::cerr << "Error: 3mf file uses a texid it hasn't defined." << std::endl;
        return ErrorCode::BAD_3MF;
    }
    std::string textureFile = itf->second;
    LOG_DEBUG(false, "    Got textureFile from dict of " + textureFile);
    LOG_DEBUG(false, "    Got texid " + std::to_string(texid) + " and id " + std::to_string(id));

    // Get the info about tiling for this texture
    auto ituv = textureModifyUVsDict.find(texid);
    if (ituv == textureModifyUVsDict.end()) {
        std::cerr << "Error: We failed to have tiling information for a texid." << std::endl;
        return ErrorCode::OTHER;
    }
    TextureInfo choice = ituv->second;
    std::array<float, 2> scaling = choice.scaling;
    Tiling tilestyleU = choice.tileU;
    Tiling tilestyleV = choice.tileV;
    LOG_DEBUG(false, "We have scaling of " << scaling[0] << ", " << scaling[1]);

    //texturePid2IdDict[id] = texid; // id here is pid on triangles
    //LOG_DEBUG(this->debug, "    Set texturePid2IdDict of id " << id << " to texid " << texid);

    // In case we need to transform coordinates after a clampTexture()
    using FP_Type = float;
    FP_Type maxU = -std::numeric_limits<FP_Type>::infinity();
    FP_Type maxV = -std::numeric_limits<FP_Type>::infinity();
    FP_Type minU = std::numeric_limits<FP_Type>::infinity();
    FP_Type minV = std::numeric_limits<FP_Type>::infinity();
    float u;
    float v;

    int coordCount = 0;
    for (auto* child = element->FirstChildElement("m:tex2coord"); child; child = child->NextSiblingElement("m:tex2coord")) {
        coordCount++;
        if (getTexCoord(child, u, v) != ErrorCode::SUCCESS) {
            std::cerr << "Error: Unable to get uv coordinate information." << std::endl;
            return ErrorCode::BAD_3MF;
        }
        maxU = std::max(maxU, u);
        minU = std::min(minU, u);
        maxV = std::max(maxV, v);
        minV = std::min(minV, v);
    }

    LOG_DEBUG(false, "    maxU " << maxU << " maxV " << maxV << " minU " << minU << " minV " << minV);

    std::vector<std::array<float, 2>> arrayOfCoords;
    arrayOfCoords.reserve(coordCount); // Do this all at once to avoid repeated allocations & memory fragmenting
    LOG_DEBUG(false, "    Reserved " << coordCount << " items of memory in arrayOfCoords");
    texture2dgroupDict[id].texturePath = textureFile; // The original file -- but it might get overwritten by clamping
    LOG_DEBUG(false, "    set texture2dgroupDict of id " << id << " to " << textureFile);
    texture2dgroupDict[id].originalTexId = texid;

    if (tilestyleU == Tiling::NONE || tilestyleV == Tiling::NONE || tilestyleU == Tiling::CLAMP || tilestyleV == Tiling::CLAMP) {
        // With a clamp or none texture we have to make a new texture file with clamping.
        if (clampTexture(texid, id, textureFile, tilestyleU == Tiling::NONE || tilestyleU == Tiling::CLAMP,
            tilestyleV == Tiling::NONE || tilestyleV == Tiling::CLAMP, maxU, maxV, minU, minV, textureFile)
            != ErrorCode::SUCCESS) {
            std::cerr << "Error: Unable to redo uv coordinate range information." << std::endl;
            return ErrorCode::OTHER;
        }
        texture2dgroupDict[id].texturePath = textureFile; // overwrite with name of new texture file from clamping
        LOG_DEBUG(false, "    Returned from clamp and set texture2dgroupDict of  id " << id << " to " << textureFile);
    }

       // The scaling might have changed after call to clampTexture, so we redo uv coordinates
    choice = textureModifyUVsDict[texid];
    scaling = choice.scaling;
    LOG_DEBUG(false, "    We now have revised transform info " << choice.scaling[0] << ", "
        << choice.scaling[1] << " for texid " << texid);

    // iterate through all siblings of the first child
    //while (child) {
    for (auto* child = element->FirstChildElement("m:tex2coord"); child; child = child->NextSiblingElement("m:tex2coord")) {
        const char* full_tag = child->Name();
        if (std::strcmp(full_tag, "m:tex2coord") == 0) {
            float u;
            float v;
            if (getTexCoord(child, u, v) != ErrorCode::SUCCESS) {
                    std::cerr << "Error: Unable to get uv coordinate information." << std::endl;
                    return ErrorCode::BAD_3MF;
            }
            LOG_DEBUG(false, "    we're going to modify using scale " << scaling[0] << " " << scaling[1]);
            LOG_DEBUG(false, "    starting with u " << u << " and v " << v);
            // Modify uvs based on transform info
            if (tilestyleU == Tiling::NONE) {
                u = (u - minU) * scaling[0];
            } else {
                u = u * scaling[0];
            }
            if (tilestyleV == Tiling::NONE) {
                v = (v - minV) * scaling[1];
            } else {
                v = v * scaling[1];
            }
            LOG_DEBUG(false, "    and now we have u " << u << " and v " << v);

            arrayOfCoords.push_back({u, v});
            LOG_DEBUG(false, "    Did push");
        }
    }

    // Assign the result to the class member
    this->texture2dgroupDict[id].coords = std::move(arrayOfCoords);

    LOG_DEBUG(this->debug, "Exiting handleTexture2dgroup.");
    return ErrorCode::SUCCESS;
}


//
// Create the texture image for a tiling style (clamp or none)
// that requires it, now that we know the max and min coordinates.
// Also modify the transform that we'll use to modify uv's as we read them in later.
// 
// Returns true for success;
//
SOP_Read3mf::ErrorCode
SOP_Read3mf::clampTexture(const int texid, const int groupId, std::string texturePathFs, bool redoU, bool redoV, float maxU, float maxV,
    float minU, float minV, std::string& usePath) {                

    LOG_DEBUG(this->debug, "Entering clampTexture.");
    LOG_DEBUG(false, "    Came in with maxU " << maxU << " maxV " << maxV);
    LOG_DEBUG(false, "    Came in with minU " << minU << " minV " << minV);

    std::string dir;
    std::string stem;
    std::string suffix;
    getFileInfo(texturePathFs, dir, stem, suffix);

    int width;
    int height;
    int numChannels;

    // load texture as it is currently
    unsigned char* originalData = stbi_load(texturePathFs.c_str(), &width, &height, &numChannels, 0);
    LOG_DEBUG(false, "    From original data we have width " << width << " and height " << height << " numChannels "
        << numChannels);

    if (!originalData) {
        std::cerr << "Error: Failed to open required texture " << texturePathFs << std::endl;
        // Figure out how to return a Houdini error XXX
        return ErrorCode::OTHER;
    }
    // After this point, we can't return arbitrarily, since we have to free the data buffer.
    // We do that at the end of the function.

    int pixelBytes = numChannels;
    int originalRowStride = width * pixelBytes;

    std::array<float, 2> currentScaling = textureModifyUVsDict[texid].scaling;

    // How much streaking do we need to do on the sides?
    int extendRight = 0;
    int extendLeft = 0;
    int extendUp = 0;
    int extendDown = 0;

    // We might already have set scaling when dealing with mirroring.
    maxU *= currentScaling[0];
    minU *= currentScaling[0];
    maxV *= currentScaling[1];
    minV *= currentScaling[1];
    LOG_DEBUG(false, "    We've reset maxU " << maxU << " maxV " << maxV);
    LOG_DEBUG(false, "    We've reset minU " << minU << " minV " << minV);

    if (maxU > 1 && redoU) {
        extendRight = ceil(width * (maxU - 1.0));
    }
    if (minU < 0 && redoU) {
        extendLeft = ceil(width * (0.0 - minU));
    }
    if (maxV > 1 && redoV) {
        extendUp = ceil(height * (maxV - 1.0)); // Because V is flipped
    }
    if (minV < 0 && redoV) {
        extendDown = ceil(height * (0.0 - minV)); // Because V is flipped
    }
    LOG_DEBUG(false, "    We have maxU " << maxU << " minU " << minU << " maxV " << maxV << " minV " << minV);
    LOG_DEBUG(false, "    We have extendLeft " << extendLeft << " extendRight " << extendRight << " extendDown "
        << extendDown << " extendUp " <<  extendUp);

    int success;

    // If clamping in both directions, we iterate through all "finalHeight" rows of the new image, and for each row,
    // we determine what data to place in the extended width of the new row.
    if (redoU && redoV) {
        int finalWidth = width + extendLeft + extendRight;
        int finalHeight = height + extendUp + extendDown;
        int finalRowStride = finalWidth * pixelBytes;
        
        std::vector<unsigned char> newData(finalWidth * finalHeight * numChannels);
        unsigned char* newImagePtr = newData.data();

        LOG_DEBUG(false, "    We have finalWidth " << finalWidth << " and finalHeight " << finalHeight
            << " and finalRowStride " << finalRowStride << " and numChannels " << numChannels);
        // Pixel data for the left-most or right-most single column pixel
        unsigned char edgePixelData[pixelBytes];
        
        // Edge rows from the original image for V-clamping
        const unsigned char* originalTopRow = originalData; // y=0
        const unsigned char* originalBottomRow = originalData + (height - 1) * originalRowStride; // y=height-1

        // Row by row, piece together what we need
        for (int y = 0; y < finalHeight; ++y) {
            unsigned char* newRow = newImagePtr + (y * finalRowStride);
            const unsigned char* sourceRow = nullptr;

            // Determine the source row for v-clamping
            if (y < extendUp) {
                // v-clamping: use the original top row
                sourceRow = originalTopRow;
            } else if (y >= (extendUp + height)) {
                // v-clamping: use the original bottom row
                sourceRow = originalBottomRow;
            } else {
                // The row is from the original image.
                // We adjust y by -extendUp to get the index into the original data
                int originalY = y - extendUp; 
                sourceRow = originalData + (originalY * originalRowStride);
            }

            // Now handle the U direction: left streak, original content, right streak.

            // For a left streak, use the first pixel of the sourceRow
            const unsigned char* leftEdgePixelPtr = sourceRow;
            std::memcpy(edgePixelData, leftEdgePixelPtr, pixelBytes);
            for (int x = 0; x < extendLeft; ++x) {
                std::memcpy(newRow + x * pixelBytes, edgePixelData, pixelBytes);
            }

            // Now the original content (or the V-streaked row)
            // Copy the full width of the sourceRow (which is originalRowStride long)
            std::memcpy(newRow + extendLeft * pixelBytes, sourceRow, originalRowStride);

            // For a right streak, use the last pixel of the sourceRow
            const unsigned char* rightEdgePixelPtr = sourceRow + (width - 1) * pixelBytes;
            std::memcpy(edgePixelData, rightEdgePixelPtr, pixelBytes);
            // The destination starts after the left streak and the copied original/streaked row data
            unsigned char* destPtr = newRow + (extendLeft * pixelBytes) + originalRowStride; 
            for (int x = 0; x < extendRight; ++x) {
                std::memcpy(destPtr + x * pixelBytes, edgePixelData, pixelBytes);
            }
        }

        success = writeNewTexture(dir, stem, suffix, "-clampedUV" + std::to_string(groupId), finalWidth, finalHeight, numChannels,
            newImagePtr, finalRowStride, usePath, this->debug);
        // Adjust both U and V scaling
        textureModifyUVsDict[texid].scaling = {1/(maxU-minU ), 1/(maxV-minV)};

    // For clamping in the U direction, we only have to worry about streaking in the left or right directions.
    } else if (redoU) {
        int finalWidth = width + extendLeft + extendRight;
        int finalHeight = height;
        int finalRowStride = finalWidth * pixelBytes;
        LOG_DEBUG(false, "    We have width " << width << " and finalWidth " << finalWidth);

        std::vector<unsigned char> newData(finalWidth * finalHeight * numChannels);
        unsigned char* newImagePtr = newData.data();

        // Pixel data for the left-most or right-most column that will be streaked
        unsigned char edgePixelData[pixelBytes];

        // Copy edge pixels on left, then original image, then edge pixels on right
        // Do this row by row
        for (int y = 0; y < height; ++y) {
            unsigned char* newRow = newImagePtr + (y * finalRowStride);
            const unsigned char* originalRow = originalData + (y * originalRowStride);

            // The first pixel of the original image is what we streak on the left side of the new image
            unsigned char* edgePixelPtr = const_cast<unsigned char*>(originalRow);
            std::memcpy(edgePixelData, edgePixelPtr, pixelBytes);
            // Now streak it on the left
            for (int x = 0; x < extendLeft; ++x) {
                if (y < 10 && x < 10) {
                    LOG_DEBUG(false, "    We're streaking left by " << pixelBytes);
                }
                std::memcpy(newRow + x * pixelBytes, edgePixelData, pixelBytes);
            }

            // Now copy the original image row
            if (y < 10) {
                LOG_DEBUG(false, "    We're copying original row to " << extendLeft * pixelBytes);
                LOG_DEBUG(false, "    It is " << originalRowStride << " long.");
            }
            std::memcpy(newRow + extendLeft * pixelBytes, originalRow, originalRowStride);

            // The last pixel of the original row is at (width - 1)
            edgePixelPtr = const_cast<unsigned char *>(originalRow) + (width - 1) * pixelBytes;
            // Streak on the right -- it's the last pixel of the original image that gets streaked
            std::memcpy(edgePixelData, edgePixelPtr, pixelBytes);
            for (int x = 0; x < extendRight; ++x) {
                if (y < 10 && x < 10) {
                    LOG_DEBUG(false, "    We're streaking right at " << (extendLeft * pixelBytes) + originalRowStride + x * pixelBytes
                        << " by " << pixelBytes);
                }
                std::memcpy(newRow + (extendLeft * pixelBytes) + originalRowStride + x * pixelBytes, edgePixelData, pixelBytes);
            }
        }
        
        success = writeNewTexture(dir, stem, suffix, "-clampedU" + std::to_string(groupId), finalWidth, finalHeight, numChannels,
            newImagePtr, finalRowStride, usePath, this->debug);
        textureModifyUVsDict[texid].scaling = {1/(maxU-minU), currentScaling[1]}; // leave tiling style entries alone
        LOG_DEBUG(false, "    we have scaling now of " << 1/maxU << " " << 1.0 << " for texid " << texid);

    // For clamping in the V direction, we only have to worry about streaking in the up or down directions.
    // This is easier than in U, since while we're in a streak section, the whole row is a streak and not
    // a pixel-by-pixel placement of data.
    } else if (redoV) { // handle none/clamp in V
        int finalHeight = height + extendUp + extendDown;
        int finalWidth = width;
        int finalRowStride = finalWidth * pixelBytes;
        LOG_DEBUG(false, "    We have height " << height << " and finalHeight " << finalHeight);

        std::vector<unsigned char> newData(finalWidth * finalHeight * numChannels);
        unsigned char* newImagePtr = newData.data();

        // Pixel data for the upper-most or lower-most row that will be streaked
        unsigned char edgePixelData[originalRowStride];

        // Start with upper most
        std::memcpy(edgePixelData, originalData, originalRowStride);
        // Copy it to the lower streak
        for (int y = 0; y < extendUp; ++y) {
            std::memcpy(newImagePtr + y * originalRowStride, edgePixelData, originalRowStride);
        }

        // Copy the full original image (width x height) to the next part of the new buffer.
        std::memcpy(newImagePtr + extendUp * originalRowStride, originalData, width * height * numChannels);
        
        // Now get pixel data for the bottom streak and copy it
        std::memcpy(edgePixelData, originalData + (height - 1) * originalRowStride, originalRowStride);
        for (int y = 0; y < extendDown; ++y) {
            std::memcpy(newImagePtr + extendUp * originalRowStride + width * height * numChannels,
                originalData + y * originalRowStride, originalRowStride);
        }

        success = writeNewTexture(dir, stem, suffix, "-clampedV" + std::to_string(groupId), finalWidth, finalHeight, numChannels,
            newImagePtr, finalRowStride, usePath, this->debug);
        textureModifyUVsDict[texid].scaling = {currentScaling[0], 1/(maxV-minV)}; // leave tiling style entries alone
        LOG_DEBUG(false, "    we have scaling now of " << 1.0 << " " << 1/maxV << " for texid " << texid);
    }

    // Free the original image memory
    stbi_image_free(originalData);
    // The new buffer doesn't need to be freed since it is a std::vector

    LOG_DEBUG(this->debug, "Exiting clampTexture.");
    if (!success) {
        return ErrorCode::OTHER;
    }
    return ErrorCode::SUCCESS;
}


//
// Parse the uv coordinate information for the texture at a point
//
SOP_Read3mf::ErrorCode
SOP_Read3mf::getTexCoord(tinyxml2::XMLElement* element, float &u, float &v) {
    
    LOG_DEBUG(false, "Entering getTexCoord");

    const char* u_cstr = element->Attribute("u");
    if (u_cstr == nullptr) {
        std::cerr << "Error: A texture coordinate has no u coordinate." << std::endl;
        return ErrorCode::BAD_3MF;
    }
    const char* v_cstr = element->Attribute("v");
    if (v_cstr == nullptr) {
        std::cerr << "Error: A texture coordinate has no v coordinate." << std::endl;
        return ErrorCode::BAD_3MF;
    }
    try {
        u = std::stof(u_cstr);
        v = std::stof(v_cstr);
    } catch (const std::invalid_argument& e) { // if string can't be converted
        std::cerr << "Error: Invalid u or v coordinate value: " << e.what() << std::endl;
        return ErrorCode::BAD_3MF;
    } catch (const std::out_of_range& e) { // too large or too small
        std::cerr << "Error: A u or v coordinate value was out of range: " << e.what() << std::endl;
        return ErrorCode::BAD_3MF;
    }
    
    LOG_DEBUG(false, "Exiting getTexCoord");
    return ErrorCode::SUCCESS;
}


//
// Handle multi-properties. Currently we only do this for object-level defaults.
//
SOP_Read3mf::ErrorCode
SOP_Read3mf::handleMultiproperties(XMLElement* element) {

    LOG_DEBUG(this->debug, "Entering handleMultiproperties");

    std::cerr << "Error: We do not yet handle multi-properties." << std::endl;

    LOG_DEBUG(this->debug, "Exiting handleMultiproperties");

    return ErrorCode::OTHER;
}


//
// Parse the build entry for the tree to see what objects should be present as
// part of the build and what their transforms should be. We'll create and record this info in a dictionary
// and then apply it later to the items themselves.
//
SOP_Read3mf::ErrorCode
SOP_Read3mf::handleBuild(XMLElement* element) {

    LOG_DEBUG(this->debug, "Entering handleBuild");

    tinyxml2::XMLElement* descendant = element->FirstChildElement();
    while (descendant != nullptr) {
        int objectID;
        const char* tag_name_cstr = descendant->Name(); // plain tag name
        std::string tag_name(tag_name_cstr ? tag_name_cstr : ""); // Safely get tag name
        if (tag_name == "item") {
            const char* objectID_cstr = descendant->Attribute("objectid");
            if (!objectID_cstr) {
                std::cerr << "Error: A build item has no object id." << std::endl;
                return ErrorCode::BAD_3MF;
            }
            try {
                objectID = std::stoi(objectID_cstr);
            } catch (const std::invalid_argument& e) { // if string can't be converted
                std::cerr << "Error: An objectID in a build item was not a valid integer: " << e.what() << std::endl;
                return ErrorCode::BAD_3MF;
            }
            const char* transform_cstr = descendant->Attribute("transform");
            UT_Matrix4 transform;
            if (!transform_cstr) {
                // The object is listed, so we need to instantiate it, but it has no transform.
                // We record an identity transform for it as a sort of empty transform, since we
                // always look first to see if it's an identity transform to avoid applying it
                // unnecessarily.
                transform.identity();
            } else {
                // Convert transform string to transform
                LOG_DEBUG(false, "    Got new transform string " << transform_cstr);
                bool success;
                success = parseTransformString(transform_cstr, transform);
                if (!success) {
                    std::cerr << "Error: Could not parse transform string for build item." << std::endl;
                    return ErrorCode::BAD_3MF;
                }
            }
            buildDict[objectID].push_back(transform);
            LOG_DEBUG(false, "    Just inserted a transform for objectID " << objectID);

            // Currently we ignore the following. Not sure if this is important for Houdini's purposes. XXX
            const char* partnumber_cstr = descendant->Attribute("partnumber");
            if (partnumber_cstr) {
                std::cerr << "Warning: We ignore part numbers in the build info for an object." << std::endl;
            }
            const char* metadatagroup_cstr = descendant->Attribute("metadatagroup");
            if (metadatagroup_cstr) {
                std::cerr << "Warning: We ignore metadatagroup info in the build info for an object." << std::endl;
            }
        } else {
            std::cerr << "Warning: Unknown tagname " << tag_name << " in build section." << std::endl;
        }
        descendant = descendant->NextSiblingElement();
    }

    LOG_DEBUG(this->debug, "Exiting handleBuild");

    return ErrorCode::SUCCESS;
}


//
// Parse the the object and set up a few Houdini datastructures.
// In particular, set up the default color for the part in case there are triangles without
// color or texture. Then go on to handle the child elements of the object.
//
SOP_Read3mf::ErrorCode
SOP_Read3mf::handleObject(XMLElement* element) {

    LOG_DEBUG(this->debug, "Entering handleObject");

    const char* id_cstr = element->Attribute("id");
    if (id_cstr == nullptr) {
        std::cerr << "Error: An object has no id." << std::endl;
        return ErrorCode::BAD_3MF;
    }

    // Get the object ID
    int id = -1;
    try {
        id = std::stoi(id_cstr);
    } catch (const std::exception& e) {
        // Handle case where 'id' exists but is not a valid integer
        std::cerr << "Error: Object ID is not a valid integer." << std::endl;
        return ErrorCode::BAD_3MF;
    }
    LOG_DEBUG(this->debug, "    for objectID " << id);

    if (objectDict.count(id) == 0) {
        ObjectData* data = new ObjectData(); // This is our only new of this object. We do the clean-up in clearData().
        data->id = id;
        objectDict[id] = data; // Store the ptr not the whole thing in case of resizing of the dictionary
        LOG_DEBUG(this->debug, "    Created objectID entry for object " << id);

        if (!this->build) { // We're not using build instructions so all objects go into the buildDict
            UT_Matrix4 transform;
            transform.identity();
            buildDict[id].push_back(transform);
            LOG_DEBUG(false, "    Just inserted a transform for objectID " << id);
        }
        LOG_DEBUG(false, "    This is a new object to us: " << id);
    } else {
        LOG_DEBUG(false, "    We've seen this object before: " << id);
    }

    // Get the type
    const char* type_cstr = element->Attribute("type");
    if (type_cstr != nullptr) {
        std::string type(type_cstr);
        if (!type.empty()) {
            if (type == "other") {
                std::cerr << "Warning: Object id " << id << " has type 'other' which we do not handle yet." << std::endl;
            } else if (type == "solidsupport" || type == "support" || type == "surface") {
                std::cerr << "Warning: Object id " << id << " has type '" << type << "' for which we do nothing special yet. Rendering is considered application-dependent." << std::endl;
            } else if (type == "model") {
                LOG_DEBUG(false, "Object id " << id << " has type 'model'.");
            } else {
                std::cerr << "Warning: Object id " << id << " has an unkown type '" + type + "'! Results might be peculiar." << std::endl;
            }
        }
    }
    /* XXX Not sure what we would do with a thumbnail anyway?
    const char* thumbnail_cstr = element->Attribute("thumbnail");
    if (thumbnail_cstr != nullptr) {
        std::string thumbnail(thumbnail_cstr);
        if (!thumbnail.empty()) {
            std::cerr << "Warning: Object id " << id << " has a thumbnail, which we ignore." << std::endl;
        }
    }
    */
    // The 3mf part numbers are strings, not integers, so even weird characters are allowed in them.
    const char* partnumber_cstr = element->Attribute("partnumber");
    if (partnumber_cstr != nullptr) {
        LOG_DEBUG(false, "Object id " << id << " has the partnumber " << partnumber_cstr << ".");
    }
    const char* name_cstr = element->Attribute("name");
    if (name_cstr != nullptr) {
        std::string name(name_cstr);
        if (!name.empty()) {
            LOG_DEBUG(false, "Object id " << id << " has the name " << name << ".");
        }
    }
    const char* pid_cstr = element->Attribute("pid");
    int pid = -1;
    if (pid_cstr != nullptr) {
        try {
            pid = std::stoi(pid_cstr);
        } catch (const std::exception& e) {
            std::cerr << "Error: The pid is not a valid integer." << std::endl;
            return ErrorCode::BAD_3MF;
        }
    }
    objectDict[id]->defaultColorGroup = pid; // defaultColorGroup will be -1 if there was no pid
    LOG_DEBUG(false, "Object id " << id << " has pid " << pid << ".");
    const char* pindex_cstr = element->Attribute("pindex");
    int pindex = -1;
    if (pindex_cstr != nullptr) {
        try {
            pindex = std::stoi(pindex_cstr);
        } catch (const std::exception& e) {
            std::cerr << "Error: The pindex is not a valid integer." << std::endl;
            return ErrorCode::BAD_3MF;
        }
        if (pindex != -1 && pid == -1) {
            std::cerr << "Error: Object " << std::to_string(id) << " has no default color or texture group but has an index into one. This is an error." << std::endl;
            return ErrorCode::BAD_3MF;
        }
    }
    
    LOG_DEBUG(false, "Object id " << id << " has pindex " << pindex << ".");

    UT_String defaultColor = DEFAULT_COLOR.data(); // This also covers the case where pid == -1
    auto itc = colorDict.find(pid);
    auto itb = basematDict.find(pid);
    auto itt = texture2dgroupDict.find(pid);
    auto itm = multiPids.find(pid);

    if (itc != colorDict.end()) {
        std::vector<std::string> colorArray = itc->second;
        std::string color = itc->second[pindex];
        LOG_DEBUG(false, "We got color " << color << " in colorDict");
        defaultColor = color;
    } else if (itb != basematDict.end()) {
        std::string displayColor = itb->second[pindex];
        defaultColor = displayColor;
        LOG_DEBUG(false, "We got displayColor " << displayColor << " in basematDict");
    } else if (itt != texture2dgroupDict.end()) {
        std::string texturePathFs;
        auto itf = texture2dgroupDict.find(pid);
        if (itf != texture2dgroupDict.end()) {
            texturePathFs = itf->second.texturePath; 
        } else {
            std::cerr << "No texture found for object  " << id << " pid " << pid << std::endl;
            return ErrorCode::OTHER;
        }

        std::vector<std::array<float, 2>> coordArray = itt->second.coords;
        if (coordArray.size() <= pindex || pindex < 0) {
            std::cerr << "Error: Index into array of texture coordinates is out of bounds." << std::endl;
            LOG_DEBUG(false, "pindex is " << pindex << " and size of coordArray is " << coordArray.size());
            return ErrorCode::BAD_3MF;
        }
        const std::array<float, 2> uvcoords = coordArray[pindex];
        LOG_DEBUG(false, "In handle object we got default texture coords of [" << uvcoords[0] << ", " << uvcoords[1] << "]");
        
        PixelColor color;
        if (colormap(texturePathFs, uvcoords, color, this->debug) != ErrorCode::SUCCESS) {
            std::cerr << "Unable to get color of pixel in default texture for objectID " << id << std::endl;
            return ErrorCode::OTHER;
        }
        LOG_DEBUG(false, "Got default object color of [" << color.r << ", " << color.g
            << ", " << color.b << "] plus alpha " << color.a);
        
        defaultColor = convertColorToHexString(color, this->debug);
        LOG_DEBUG(false, "Converted default color to hex string: " << defaultColor);

    } else if (itm != multiPids.end()) {
        std::cerr << "We do not yet handle multi-properties." << std::endl;
        return ErrorCode::OTHER;
    }

    objectDict[id]->defaultColor = defaultColor;

    // Now handle child elements of the object
    tinyxml2::XMLElement* descendant = element->FirstChildElement();
    while (descendant != nullptr) {
        const char* tag_name_cstr = descendant->Name(); 
        std::string tag_name(tag_name_cstr ? tag_name_cstr : ""); 
        
        if (tag_name == "mesh") {
            if (objectDict[id]->objGdpHandle.isValid()) {
                LOG_DEBUG(false, "    We've already built object id " << id);
                descendant = descendant->NextSiblingElement();
                continue;
            }

            GU_Detail* new_gdp = new GU_Detail();
            objectDict[id]->objGdpHandle.allocateAndSet(new_gdp, true);
            GU_Detail* objGdp = objectDict[id]->objGdpHandle.gdpNC();

            LOG_DEBUG(false, "    Added objGdp for object id " << id);
            
            int numVertices = 0;
            int numTriangles = 0;
            if (handleMesh(descendant, numVertices, numTriangles, id) != ErrorCode::SUCCESS) {
                std::cerr << "Error: Unable to handle mesh object." << std::endl;
                return ErrorCode::BAD_3MF;
            }
            LOG_DEBUG(false, "For the mesh of object " << id << " we have "
                << numVertices << " points and " << numTriangles << " triangles.");

        } else if (tag_name == "metadatagroup") {
            std::cerr << "Warning: We do not yet handle metadatagroups, so results might be suspect." << std::endl;
        } else if (tag_name == "components") {
            if (!objectDict[id]->objGdpHandle.isValid()) {
                GU_Detail* new_gdp = new GU_Detail();
                objectDict[id]->objGdpHandle.allocateAndSet(new_gdp, true);
                GU_Detail* objGdp = objectDict[id]->objGdpHandle.gdpNC();

                LOG_DEBUG(false, "    Added objGdp for object id " << id);
            }
            if (handleComponents(descendant, id) != ErrorCode::SUCCESS) {
                std::cerr << "Error: Could not parse components of an object." << std::endl;
                return ErrorCode::OTHER;
            }
        } else {
            std::cerr << "Warning: Ignoring unrecognized tag name " << tag_name << ", so results might be suspect." << std::endl;
        }

        descendant = descendant->NextSiblingElement();
    }

    LOG_DEBUG(this->debug, "Exiting handleObject");

    return ErrorCode::SUCCESS;
}


//
// Parse a mesh part of the tree and set up Houdini datastructures.
//
SOP_Read3mf::ErrorCode
SOP_Read3mf::handleMesh(XMLElement* element, int& numVertices, int& numTriangles, const int objID) {
    LOG_DEBUG(this->debug, "Entering handleMesh");

    tinyxml2::XMLElement* descendant = element->FirstChildElement();
    while (descendant != nullptr) {
        const char* tag_name_cstr = descendant->Name(); // plain tag name
        std::string tag_name(tag_name_cstr ? tag_name_cstr : ""); // Safely get tag name
        LOG_DEBUG(false, "     Got tagname " << tag_name);
        if (tag_name == "vertices") {
            if (handleVertices(descendant, numVertices, objID) != ErrorCode::SUCCESS) {
                std::cerr << "Error: Unable to handle a vertex for object ID " << objID << "." << std::endl;
                return ErrorCode::BAD_3MF;
            }
        } else if (tag_name == "triangles") {
            if (handleTriangles(descendant, numTriangles, objID) != ErrorCode::SUCCESS) {
                std::cerr << "Error: Unable to handle a triangle for object ID " << objID << "." << std::endl;
                return ErrorCode::BAD_3MF;
            }
        }
        descendant = descendant->NextSiblingElement();
    }

    /*
    # If we've used color on some faces and texture on others, then we need
    # to make sure the color that Houdini will create on the textured faces
    # is white so we don't interfere with the texture.
    // Add this XXX
    shop_attrib = geo.findPrimAttrib("shop_materialpath")
    color_attrib = geo.findVertexAttrib("Cd")
    if shop_attrib is not None and color_attrib is not None:
        for prim in geo.prims():
            #get texture attrib
            value = prim.stringAttribValue(shop_attrib)
            if value != "":
                for v in prim.vertices():
                    recolor_attrib = geo.findVertexAttrib("recolor")
                    if recolor_attrib is not None:
                        v.setAttribValue(color_attrib, WHITE_ARRAY)
                
    */
    LOG_DEBUG(this->debug, "Exiting handleMesh");

    return ErrorCode::SUCCESS;
}


//
// Parse the components section of an object.
//
SOP_Read3mf::ErrorCode
SOP_Read3mf::handleComponents(XMLElement* element, int parentID) {
    LOG_DEBUG(this->debug, "Entering handleComponents");

    tinyxml2::XMLElement* descendant = element->FirstChildElement();
    while (descendant != nullptr) {
        const char* tag_name_cstr = descendant->Name(); // plain tag name
        std::string tag_name(tag_name_cstr ? tag_name_cstr : ""); // Safely get tag name
        if (tag_name == "component") {
            if (handleComponent(descendant, parentID) != ErrorCode::SUCCESS) {
                std::cerr << "Error: Failed to handle a component of an object." << std::endl;
                return ErrorCode::BAD_3MF;
            } 
        } else {
                std::cerr << "Error: A object components section contained a tag other than a component." << std::endl;
                return ErrorCode::BAD_3MF;
        }
        descendant = descendant->NextSiblingElement();
    }

    LOG_DEBUG(this->debug, "Exiting handleComponents");

    return ErrorCode::SUCCESS;
}


//
// Parse an object that consists of other components, which are objects. This can be recursive.
// We do not yet handle objects in other model files than the main one.
//
SOP_Read3mf::ErrorCode
SOP_Read3mf::handleComponent(XMLElement* element, int parentID) {
    LOG_DEBUG(this->debug, "Entering handleComponent");

    const char* id_cstr = element->Attribute("objectid");
    if (id_cstr == nullptr) {
        std::cerr << "Error: A component has no object id." << std::endl;
        return ErrorCode::BAD_3MF;
    }
    int id = -1;
    try {
        id = std::stoi(id_cstr);
    } catch (const std::exception& e) {
        std::cerr << "Error: Failed to convert an object id to an integer." << std::endl;
        return ErrorCode::BAD_3MF;
    }
    LOG_DEBUG(false, "    We got component object id " << id);
    const char *xform_cstr = element->Attribute("transform");
    if (xform_cstr == nullptr) {
        LOG_DEBUG(false, "    with no xform");
    }

    // Merge object with id into parentID's gdp applying the transform we found

    // Look up the ID once
    auto it = objectDict.find(id);
    // Check if it exists in the map.
    if (it == objectDict.end()) {
        std::cerr << "Error: Component object " << id << " has not already been built." << std::endl;
        return ErrorCode::OTHER;
    }
    // Check if the handle actually contains geometry.
    if (it->second->objGdpHandle.isNull()) {
        std::cerr << "Error: Component object " << id << " has not yet been given a GU_Detail." << std::endl;
        return ErrorCode::OTHER;
    }
    auto itp = objectDict.find(parentID);
    // Check if it exists in the map.
    if (itp == objectDict.end()) {
        std::cerr << "Error: We encountered a parent of a component object that has not already been entered." << std::endl;
        return ErrorCode::OTHER;
    }
    // Check if the handle actually contains geometry.
    if (itp->second->objGdpHandle.isNull()) {
        std::cerr << "Error: We encountered a component object whose parent has not yet been given a GU_Detail.." << std::endl;
        return ErrorCode::OTHER;
    }

    // Use the .gdp() method from the header for read-only access
    const GU_Detail* sourceGdp = it->second->objGdpHandle.gdp();
    // Ensure this handle is the only one pointing to this geometry
    itp->second->objGdpHandle.makeUnique();
    GU_Detail* parentGdp = itp->second->objGdpHandle.gdpNC(); // Non-const, since we'll change it

    if (!sourceGdp || !parentGdp) {
        std::cerr << "Error: No geometry detail for an object or objects." << std::endl;
        return ErrorCode::OTHER;
    }

    // Capture current point count before merge
    GA_Offset start_pt_off = parentGdp->getNumPoints();

    // Do the merge
    parentGdp->merge(*sourceGdp);

    // Capture new count
    GA_Offset end_pt_off = parentGdp->getNumPoints();

    // Create the range using the offsets
    GA_Range new_points(parentGdp->getPointMap(), start_pt_off, end_pt_off);

    // Get component object's transform
    UT_Matrix4 transform;
    if (!xform_cstr) {
        transform.identity();
    } else {
        bool success;
        success = parseTransformString(xform_cstr, transform);
        if (!success) {
            std::cerr << "Error: Could not parse transform string for build item." << std::endl;
            return ErrorCode::BAD_3MF;
        }
    }

    // Transform the points
    parentGdp->transform(
        transform,              // The matrix
        GA_Range(),             // Argument 2: Empty Primitive Range
        new_points,             // Argument 3: Our New Point Range
        false                   // Argument 4: just_P (false means transform vectors too)
    );

    LOG_DEBUG(false, "    Merge gdp from object " << id << " into parent of id " << parentID);

    LOG_DEBUG(this->debug, "Exiting handleComponent");

    return ErrorCode::SUCCESS;
}


//
// Process vertices in the mesh.
//
SOP_Read3mf::ErrorCode
SOP_Read3mf::handleVertices(XMLElement* element, int& numVertices, const int objID) {
    LOG_DEBUG(false, "Entering handleVertices");
    tinyxml2::XMLElement* descendant = element->FirstChildElement();
    while (descendant != nullptr) {
        const char* tag_name_cstr = descendant->Name(); // plain tag name
        std::string tag_name(tag_name_cstr ? tag_name_cstr : ""); // Safely get tag name
        LOG_DEBUG(false, std::string("     Got tagname ") + tag_name);
        if (tag_name == "vertex") {
            double xpos = 0.0, ypos = 0.0, zpos = 0.0;
            XMLError result_x = descendant->QueryDoubleAttribute("x", &xpos);
            XMLError result_y = descendant->QueryDoubleAttribute("y", &ypos);
            XMLError result_z = descendant->QueryDoubleAttribute("z", &zpos);

            if (result_x != XML_SUCCESS || result_y != XML_SUCCESS || result_z != XML_SUCCESS) {
                std::cerr << "Error: A coordinate for a vertex is missing or invalid." << std::endl;
                return ErrorCode::BAD_3MF;
            }
            std::vector<float> pos;
            // Using static_cast to explicitly convert double (read from XML) to float
            pos.push_back(static_cast<float>(xpos));
            pos.push_back(static_cast<float>(ypos));
            pos.push_back(static_cast<float>(zpos));
            UT_Vector3 p_coord(xpos, ypos, zpos);
            
            this->vertexDict[numVertices] = pos;
            LOG_DEBUG(false, std::string("    Got vertex pos ") + std::to_string(xpos) + ", " + std::to_string(ypos)
                + ", " + std::to_string(zpos) + std::string(" for vertex # ")
                + std::to_string(numVertices));

            // We now create the actual points in handleTriangle()
            // as compared to the python version.

            ++numVertices;
        } else {
            std::cerr << "We found a non-vertex tag " << tag_name << " in the vertex section of the mesh." << std::endl;
            return ErrorCode::BAD_3MF;
        }
        descendant = descendant->NextSiblingElement();
    }

    LOG_DEBUG(false, "Exiting handleVertices");

    return ErrorCode::SUCCESS;
}


//
// Process triangles in the mesh. We set up a bunch of the attribute handles here so we don't need to test and create
// them in handleTriangle() for each triangle. Currently I end up with these attributes always, even if this part
// doesn't need them (like texture, for instance). I need to figure out how to avoid that, but that's a next step. XXX
//
SOP_Read3mf::ErrorCode
SOP_Read3mf::handleTriangles(XMLElement* element, int& numTriangles, const int objID) {
    LOG_DEBUG(this->debug, "Entering handleTriangles");
    
    auto ito = objectDict.find(objID);
    if (ito == objectDict.end() || !ito->second->objGdpHandle.isValid()) {
        std::cerr << "Error: Missing necessary information for an object." << std::endl;
        return ErrorCode::OTHER;
    }
    // Check if the handle actually contains geometry.
    if (ito->second->objGdpHandle.isNull()) {
        std::cerr << "Error: Missing a GU_Detail for object." << std::endl;
        return ErrorCode::OTHER;
    }
    GU_Detail* objGdp = ito->second->objGdpHandle.gdpNC(); // non-const since we'll be changing it
    LOG_DEBUG(this->debug, "    We currently have " << objGdp->getNumPoints() << " points, "
        << objGdp->getNumVertices() << " vertices, and " << objGdp->getNumPrimitives() << " prims");


    // By default we give a -1 object id. Primitives in objects with ids in the build items will
    // have a proper positive object id. The ones that aren't there will be left with the default.
    // (All primitives will have an object id attribute if any of them do.)
    GA_RWHandleID obj_h = GA_RWHandleID(objGdp->addIntTuple(GA_ATTRIB_PRIMITIVE, "object_id", 1, GA_Defaults(-1)));
    if (!obj_h.isValid()) {
        addError(SOP_MESSAGE, "object_id attribute handle is invalid after its creation.");
        std::cerr << "Error: object_id attribute handle is invalid for object " << objID << " after its creation." << std::endl;
        return ErrorCode::OTHER;
    }

    // XXX Check somehow if we're *really* using color on  triangles. Alas, that check happens for real
    // in handleTriangle(), but it's expensive to do all this for every triangle, so we do it here.
    // When this was in handleTriangle() I tested if we'd already created it by trying to find an existing vertex color attribute
    //GA_RWHandleV3 Cd_h(objGdp->findDiffuseAttribute(GA_ATTRIB_VERTEX));
    //if (!Cd_h.isValid()) {
    // create it
    //}
    // Does the above guarantee it's a color attribute?? XXX
    //GA_RWHandleV3 Cd_h(objGdp->findDiffuseAttribute(GA_ATTRIB_VERTEX));
    GA_RWHandleV3 Cd_h = GA_RWHandleV3(objGdp->addFloatTuple(GA_ATTRIB_VERTEX, "Cd", 3, GA_Defaults(1.0)));
    Cd_h->setTypeInfo(GA_TYPE_COLOR);
    if (!Cd_h.isValid()) {
        addError(SOP_MESSAGE, "Color attribute handle is invalid after its creation.");
        std::cerr << "Error: Color attribute handle is invalid after its creation for object " << objID << std::endl;
        return ErrorCode::OTHER;
    }

    // I should be able to use GEO_STD_ATTRIB_TEXTURE in place of "uv", but it seems I can't

    GA_RWHandleV3 UV_h = GA_RWHandleV3(objGdp->addFloatTuple(GA_ATTRIB_VERTEX, "uv", 3, GA_Defaults(0.0)));
    if (!UV_h.isValid()) {
        addError(SOP_MESSAGE, "UV attribute handle is invalid after its creation.");
        std::cerr << "Error: UV attribute handle is invalid for object " << objID << " after its creation." << std::endl;
        return ErrorCode::OTHER;
    }
    UV_h->setTypeInfo(GA_TYPE_TEXTURE_COORD);

    // I should be able to use GEO_STD_ATTRIB_MATERIAL in place of "shop_materialpath", but it seems I can't
    //GA_RWHandleS material_h = GA_RWHandleS(objGdp->addStringTuple(GA_ATTRIB_PRIMITIVE, "shop_materialpath", 1));
    GA_RWHandleS material_h = GA_RWHandleS(objGdp->addStringTuple(GA_ATTRIB_PRIMITIVE, "shop_materialpath", 1));
    if (!material_h.isValid()) {
        addError(SOP_MESSAGE, "material path attribute handle is invalid after its creation.");
        std::cerr << "Error: material path attribute handle is invalid for object " << objID << " after its creation." << std::endl;
        return ErrorCode::OTHER;
    }

    // XXX For strings Houdini has to do a lookup to see if the string already exists so it can use the right index
    // for the string in the attribute. So maybe I should do this once for all the primitives in the new range
    // once I leave handleTriangle() XXX
    // Create an attribute to store the specific texture path for each primitive
    GA_RWHandleS override_h(objGdp->addStringTuple(GA_ATTRIB_PRIMITIVE, "material_override", 1));
    if (!override_h.isValid()) {
        addError(SOP_MESSAGE, "override attribute handle is invalid after its creation.");
        std::cerr << "Error: override attribute handle is invalid for object " << objID << " after its creation." << std::endl;
        return ErrorCode::OTHER;
    }

    UT_Map<PointKey, GA_Offset> localPointDict;

    tinyxml2::XMLElement* descendant = element->FirstChildElement();
    LOG_DEBUG(false, "    got next descendent element");
    while (descendant != nullptr) {
        const char* tag_name_cstr = descendant->Name(); // plain tag name
        std::string tag_name(tag_name_cstr ? tag_name_cstr : ""); // Safely get tag name
        LOG_DEBUG(false, std::string("     Got tagname ") + tag_name);
        if (tag_name == "triangle") {
            if (handleTriangle(descendant, numTriangles, objID, objGdp, localPointDict, obj_h, Cd_h, UV_h,
                material_h, override_h) != ErrorCode::SUCCESS) {
                std::cerr << "Error: Unable to handle a triangle for object ID " << objID << "." << std::endl;
                return ErrorCode::BAD_3MF;
            }
            ++numTriangles;
        } else {
            std::cerr << "We found a non-triangle tag " << tag_name << " in the triangle section of the mesh." << std::endl;
            return ErrorCode::BAD_3MF;
        }
        descendant = descendant->NextSiblingElement();
    }
    
    /* // For debugging / testing
    if (this->debug && !objGdp->checkValidTopology()) {
        LOG_DEBUG(true, "Error: Topology is invalid after building primitives.");
        return ErrorCode::OTHER;
    }
    */

    LOG_DEBUG(this->debug, "    We now have " << objGdp->getNumPoints() << " points, "
        << objGdp->getNumVertices() << " vertices, and " << objGdp->getNumPrimitives() << " prims");

    LOG_DEBUG(this->debug, "Exiting handleTriangles");

    return ErrorCode::SUCCESS;
}


//
// Add a primitive to the mesh. This is where all the interesting stuff happens.
// The pid of the triangle points to a colorgroup/texture2dgroup/multiproperties
// The pindices (p1, p2, p3) have further indexing info for each vertex of the triangle
// and the meaning of those pindices changes depending on whether it's a color, texture,
// or multiproperty we're handling. We add this triangle to every copy of its object that
// is an item in the buildDict, but it might have a transform on it for those different
// copies.
// Coming into this, the object has already been assigned a default color, based either on
// an entry in a color group or a coordinate in an entry in a texture group. If there wasn't
// one of those, it has been assigned a global default color and its defaultColorGroup will
// be -1. We also know that the attribute handles passed as parameters have been created and are valid.
//
SOP_Read3mf::ErrorCode
SOP_Read3mf::handleTriangle(XMLElement* element, int& numTriangles, const int objID, GU_Detail* objGdp,
    UT_Map<PointKey, GA_Offset>& pointDict, GA_RWHandleID obj_h,
    GA_RWHandleV3 Cd_h, GA_RWHandleV3 UV_h, GA_RWHandleS material_h, GA_RWHandleS override_h) {

    LOG_DEBUG(false, "Entering handleTriangle");

    UT_String defaultColor = this->objectDict[objID]->defaultColor;
    LOG_DEBUG(false, "    Got default color of " << defaultColor << " from object");
    LOG_DEBUG(false, "    We currently have " << objGdp->getNumPoints() << " points, "
        << objGdp->getNumVertices() << " vertices, and " << objGdp->getNumPrimitives() << " prims");


    // This can be confusing, since the 3mf format numbers the vertices from 1 rather than 0.
    // These are indices into the list of vertices rather than being the vertex info themselves.
    UT_FixedArray<int, 3> verts;
    verts[0] = -1;
    verts[1] = -1;
    verts[2] = -1;

    // Use QueryIntAttribute to check for existence and safe conversion simultaneously.
    tinyxml2::XMLError result_v1 = element->QueryIntAttribute("v1", &verts[0]);
    tinyxml2::XMLError result_v2 = element->QueryIntAttribute("v2", &verts[1]);
    tinyxml2::XMLError result_v3 = element->QueryIntAttribute("v3", &verts[2]);

    if (this->flip) {
        int buffer;
        buffer = verts[0];
        verts[0] = verts[2];
        verts[2] = buffer;
    }

    // Check if any of the three required attributes are missing or invalid (non-integer).
    if (result_v1 != tinyxml2::XML_SUCCESS || result_v2 != tinyxml2::XML_SUCCESS || result_v3 != tinyxml2::XML_SUCCESS) {

        std::cerr << "Error: A triangle is missing a required vertex index (v1, v2, or v3) or the index is invalid."
            << std::endl;
        return ErrorCode::BAD_3MF;
    }
    LOG_DEBUG(false, std::string("    We have v1: ") + std::to_string(verts[0]) + std::string(" v2: ") + std::to_string(verts[1])
        + std::string(" v3: ") + std::to_string(verts[2]));

    // Add point coordinates to vertexDict. (They are called vertices in 3mf but points in Houdini.)
    UT_FixedArray<UT_Vector3, 3> posList;
    for (exint i = 0; i < 3; ++i) {
        // Correct use of .find() to avoid "Auto-Insertion" of empty entries
        auto it = vertexDict.find(verts[i]);

        // Safety Check: Verify the vertex ID actually exists in the dictionary
        if (it == vertexDict.end()) {
            std::cerr << "Error: Triangle refers to vertex ID " << verts[i] 
                      << " which was never defined in the mesh." << std::endl;
            return ErrorCode::BAD_3MF;
        }

        // Get reference to the vector stored in the map iterator (it->second)
        const std::vector<float>& vert_coords = it->second;

        // Critical Size Check: Prevent out-of-bounds access on vert_coords[2]
        if (vert_coords.size() < 3) {
            std::cerr << "Error: Vertex ID " << verts[i] 
                      << " has only " << vert_coords.size() 
                      << " coordinates (3 expected)." << std::endl;
            return ErrorCode::OTHER;
        }

        // Safe Assignment: We now know indices 0, 1, and 2 exist
        posList[i].assign(vert_coords[0], vert_coords[1], vert_coords[2]);

        LOG_DEBUG(false, "    Vertex " + std::to_string(i) + " (ID:" + std::to_string(verts[i]) + 
                              ") pos: " + std::to_string(vert_coords[0]) + ", " + 
                              std::to_string(vert_coords[1]) + ", " + 
                              std::to_string(vert_coords[2]));
    }
   
    // Note that if there's already a point of the same position on this object, we reuse it,
    // since we'd otherwise get a coincident point for every vertex at this point. If there's
    // no point yet in that position for this object, create one.
    PointKey lookupKey;
    LOG_DEBUG(false, "    Detail currently has " + std::to_string(objGdp->getNumVertices()) + " vertices.");
    // Manually append vertices and wire them to points
    GA_Offset p_offsets[3];
    for (exint i = 0; i < 3; i++) { // cycle through vertices
        lookupKey.pos = posList[i];
        LOG_DEBUG(false, "    Got " + std::to_string(i) + "th lookup pos of {" + std::to_string(lookupKey.pos[0])
            + ", " + std::to_string(lookupKey.pos[1]) + ", " + std::to_string(lookupKey.pos[2]) + "}");
        LOG_DEBUG(false, "    Got " + std::to_string(i) + "th lookup objID");
        auto itp = pointDict.find(lookupKey);
        if (itp != pointDict.end()) { // We already have this point
            p_offsets[i] = itp->second;
            LOG_DEBUG(false, std::string("    We already have point ") + std::to_string(p_offsets[i]));
        } else { // We haven't seen this point yet in this copy of the object
            LOG_DEBUG(false, "    No itp entry or we haven't seen this point before in this copy of the object");
            // New point. Add it to the dictionary and set the vertex to it and set its position.
            p_offsets[i] = objGdp->appendPoint();
            LOG_DEBUG(false, "    Got p_offset " + std::to_string(p_offsets[i]) + " for new point.");
            objGdp->setPos3(p_offsets[i], posList[i]);
            LOG_DEBUG(false, "    Set the position of the point to " << posList[i] << " for position " << i);
            pointDict[lookupKey] = p_offsets[i];
            LOG_DEBUG(false, "    added offset " + std::to_string(p_offsets[i]) + " to pointDict.");
        }
        //poly->appendVertex(p_offset);
        //poly->setPointOffset(i, p_offset);

        LOG_DEBUG(false, "    Added vertex.");
    }

    // Create the polygon
    //GU_PrimPoly *poly = GU_PrimPoly::build(objGdp, 3, p_offsets, false);
    GU_PrimPoly *poly = (GU_PrimPoly *)objGdp->appendPrimitive(GA_PRIMPOLY);
    poly->appendVertex(p_offsets[0]);
    poly->appendVertex(p_offsets[1]);
    poly->appendVertex(p_offsets[2]);

    // Mark it as closed (required for a face)
    poly->close();
    GA_Offset primOff = poly->getMapOffset();

    /* // For testing/debuging
    if (this->debug && primOff % 10 == 0) {
        // validate() checks the internal topology and attribute pages
        if (!objGdp->checkValidTopology()) {
            LOG_DEBUG(true, "CRITICAL: Geometry corrupted at triangle index: " << primOff);
            // If it's corrupt, stop immediately so you can inspect the previous log entries
            return ErrorCode::OTHER; 
        }
    }
    */
    LOG_DEBUG(false, "    After bulding poly, we have " << objGdp->getNumPoints() << " points, "
        << objGdp->getNumVertices() << " vertices, and " << objGdp->getNumPrimitives() << " prims");
    obj_h.set(primOff, objID);
    LOG_DEBUG(false, "    Got " + std::to_string(primOff) + " for the prim offset.");

    //
    // Now Look for texture/color/etc. on the triangles.
    //
    // Assign default values for operations below. In Houdini we use vertex color totally and son't try to optimize
    // by using point or prim or detail color. It is hard at this point
    // in the logic to tell if something else will use vertex color in the model, and all the others can be built off
    // of vertex color in any case, although it might take up more attribute space.
    // Coming into this routine the object has been given a default color and default colorgroup or texturegroup id.
    //
    UT_String color = defaultColor;
    bool useTexture = false;
    bool useMulti = false;
    std::vector<std::string> colorArray;
    std::vector<std::array<float, 2>> coordArray;
    std::vector<std::string> basematArray;
    std::vector<int> multiArray;

    int pid = -1;
    int groupID;
    tinyxml2::XMLError result_pid = element->QueryIntAttribute("pid", &pid);
    if (result_pid != tinyxml2::XML_SUCCESS) {
        // A groupID just overrides the object default color's groupID, so when missing a pid, use that.
        groupID = objectDict[objID]->defaultColorGroup;
        LOG_DEBUG(false, "    No id for color or texture group so using default");
    } else {  
        groupID = pid;
        LOG_DEBUG(false, "    Triangle has pid on it, use that: " + std::to_string(pid));
    }
    LOG_DEBUG(false, std::string("    We have pid ") + std::to_string(pid));
    LOG_DEBUG(false, std::string("    We have groupID ") + std::to_string(groupID));

    // What kind of color/texture does this triangle have, if any?

    auto itc = this->colorDict.find(groupID);
    auto itb = this->basematDict.find(groupID);
    auto itt = this->texture2dgroupDict.find(groupID);
    auto itm = this->multiPids.find(groupID);
    if (itt != this->texture2dgroupDict.end()) { // Pick which texture2dgroup to use for the triangle
        coordArray = itt->second.coords;
        if (!coordArray.size()) {
            std::cerr << "Error: Array of uv corrdinates for this texture group has size 0" << std::endl;
            return ErrorCode::OTHER;
        }
        for (int i = 0; i < coordArray.size(); ++i) {
            LOG_DEBUG(false, "        Coordarray of " + std::to_string(i) + " is " + std::to_string(coordArray[i][0])
                + ", " + std::to_string(coordArray[i][1]));
        }
        LOG_DEBUG(false, "    We got texture");
        useTexture = true;
    } else if (itc != colorDict.end()) {
        colorArray = itc->second;
        LOG_DEBUG(false, std::string("We got color "));
    } else if (itb != basematDict.end()) {
        basematArray = itb->second;
        LOG_DEBUG(false, std::string("We got basemat "));
    } else if (itm != multiPids.end()) {
        multiArray = itm->second;
        LOG_DEBUG(false, std::string("We got multi "));
        useMulti = true;
    } else if (groupID != -1) { // We were reset by something, but it's not something we recognize
        LOG_DEBUG(true, std::string("Warning: A triangle has an unknown property: ") + std::to_string(groupID)
            + std::string(" -- assigning default color"));
        groupID = -1;
    } // Otherwise there's nothing, so we leave groupID at -1 so we get the default color

    if (groupID == -1) {
        // Force us to use color on the triangle.
        useTexture = false;
        useMulti = false;
        LOG_DEBUG(false, "    Setting texture and multi to false, since we're using default object color");
    }

    // Grab / set the pindices that provide info for each vertex of the triangle
    int p1;
    int p2;
    int p3;

    tinyxml2::XMLError result_p1 = element->QueryIntAttribute("p1", &p1);
    if (result_p1 != tinyxml2::XML_SUCCESS) {
        p1 = -1;
    }
    tinyxml2::XMLError result_p2 = element->QueryIntAttribute("p2", &p2);
    tinyxml2::XMLError result_p3 = element->QueryIntAttribute("p3", &p3);
    if (result_p2 != tinyxml2::XML_SUCCESS || result_p3 != tinyxml2::XML_SUCCESS) {
        p2 = p3 = p1; // SPEC says: If p2 or p3 is unspecified then p1 is used for the entire triangle.
    }
    if (groupID == -1 && p1 != -1) { // Is this what I should do? Or let obj default override? But indices could be wrong for that XXX
        LOG_DEBUG(true, "Warning: A triangle has no ID signifying which color/texture/multi group to use, but it specified indices into an unknown group. Assigning default color.");
        p1 = p2 = p3 = -1; // Use default color from the object
    }
    if (p1 == -1 && p2 == -1 && p3 == -1) { // We have to use the object default color
        useTexture = false;
        useMulti = false;
        LOG_DEBUG(false, "    Setting texture and multi to false so we'll use color");
    }

    //
    // Using color
    //
    if (!useTexture && !useMulti) { // We're using color
        LOG_DEBUG(false, "    We are doing color");
        
        // The pindices choose a color for this point
        // lambda function to avoid writing this 3 times for 3 vertices
        auto assign_vertex_color = [&](int local_vtx_idx, int basemat_p) -> void {
            std::string color;
            if (groupID == -1 || basemat_p == -1) {
                color = defaultColor;
                LOG_DEBUG(false, "    groupID and basemat -1 so assigning default color");
            } else if (itb != basematDict.end()) {  // Check for base mat color
                const std::vector<std::string>& basematArray = itb->second;
                if (basematArray.size() < (size_t) basemat_p + 1) {
                    color = defaultColor;
                    LOG_DEBUG(false, "    basemat default");
                } else {
                    color = basematArray[basemat_p];
                    LOG_DEBUG(false, "    basemat");
                }

            } else {
                color = colorArray[basemat_p];
                LOG_DEBUG(false, "    from basemat");
            }
            float alpha;
            UT_Vector3 aColor = convertHexStringToUTVector3(color, alpha, false);
            GA_Offset global_vtx_off = poly->getVertexOffset(local_vtx_idx);
            Cd_h.set(global_vtx_off, aColor);
            LOG_DEBUG(false, "    Set vertex color to " + color);
        };
        if (this->flip) { // Where to flip what is a little puzzling. I appears we need to do so again.
            assign_vertex_color(0, p3);
            assign_vertex_color(1, p2);
            assign_vertex_color(2, p1);
        } else {
            assign_vertex_color(0, p1);
            assign_vertex_color(1, p2);
            assign_vertex_color(2, p3);
        }
        LOG_DEBUG(false, "    We now have " << objGdp->getNumPoints() << " points, "
            << objGdp->getNumVertices() << " vertices, and " << objGdp->getNumPrimitives() << " prims");


        return ErrorCode::SUCCESS;
    }

    //
    // Multi-properties. Currently we don't handle these on individual triangles -- only when they are default
    // object multi-properties
    //
    if (useMulti) {
        std::cerr << "Warning: We currently do not handle multi-properties on individual mesh triangles." << std::endl;
        // XXX The python seems to exit at this point, but I'm not sure about that.
        return ErrorCode::SUCCESS;
    }

    //
    // From here on it's regular textures.
    //
    LOG_DEBUG(false, std::string("    Entering texture section we have p1 ") + std::to_string(p1) + std::string(", p2 ")
        + std::to_string(p2) + std::string(", p3 ") + std::to_string(p3) + std::string(" and default color ")
        + std::string(defaultColor));

    if (p1 == -1) {
        // Assign default object property: ToDo XXX
        ;
    }
    if (p2 == -1) {
        p2 = p1;
    }
    if (p3 == -1) {
        p3 = p1;
    }

    // coordArray contains the uvs
    std::array<float, 2> uv1;
    std::array<float, 2> uv2;
    std::array<float, 2> uv3;
    if (this->flip) {
        uv1 = coordArray[p3];
        uv2 = coordArray[p2];
        uv3 = coordArray[p1];
    } else {
        uv1 = coordArray[p1];
        uv2 = coordArray[p2];
        uv3 = coordArray[p3];
    }
    /*
    auto assign_uv = [&](int local_vtx_idx, int p_idx) {
        if (p_idx >= 0 && p_idx < coordArray.size()) {
            UT_Vector3 uv_vec(coordArray[p_idx][0], coordArray[p_idx][1], 0.0f);
            
            GA_Offset global_vtx_off = poly->getVertexOffset(local_vtx_idx);
            UV_h.set(global_vtx_off, uv_vec);
        }
    };
    */

    int vertex = verts[0];
    UT_Vector3T<float> coords;
    coords[0] = uv1[0];
    coords[1] = uv1[1];
    coords[2] = 0.0;
    GA_Offset global_vtx_off = poly->getVertexOffset(0);
    LOG_DEBUG(false, "    For vertex 0 with offset " << global_vtx_off << " we have coords " << coords);
    UV_h.set(global_vtx_off, coords);

    vertex = verts[1];
    coords[0] = uv2[0];
    coords[1] = uv2[1];
    coords[2] = 0.0;
    global_vtx_off = poly->getVertexOffset(1);
    LOG_DEBUG(false, "    For vertex 1 with offset " << global_vtx_off << " we have coords " << coords);
    UV_h.set(global_vtx_off, coords);

    vertex = verts[2];
    coords[0] = uv3[0];
    coords[1] = uv3[1];
    coords[2] = 0.0;
    global_vtx_off = poly->getVertexOffset(2);
    LOG_DEBUG(false, "    For vertex 2 with offset " << global_vtx_off << " we have coords " << coords);
    UV_h.set(global_vtx_off, coords);

    if (material_h.isValid() && this->shaderNode.length() > 0) {
        material_h.set(primOff, this->shaderNode);
        LOG_DEBUG(false, "    Setting materialpath to " << this->shaderNode);
    }

    LOG_DEBUG(false, "    Before texturegroup thing.");
    auto ittp = texture2dgroupDict.find(groupID);
    if (ittp != texture2dgroupDict.end()) {
        UT_String path = ittp->second.texturePath;
        LOG_DEBUG(false, "    Got texturePath " << path);
        std::string jsonStr = "{\"basecolor_useTexture\":1, \"basecolor_texture\":\"" + std::string(path.buffer()) + "\"}";
        override_h.set(primOff, jsonStr.c_str());
        LOG_DEBUG(false, "    Back from set");
    } else {
        std::cerr << "Error: No texture available for primitive -- it should already have been set." << std::endl;
        return ErrorCode::OTHER;
    }

    LOG_DEBUG(false, "    We now have " << objGdp->getNumPoints() << " points, "
        << objGdp->getNumVertices() << " vertices, and " << objGdp->getNumPrimitives() << " prims");

    LOG_DEBUG(false, "Exiting handleTriangle");

    return ErrorCode::SUCCESS;
}
} // end HDK_Sample