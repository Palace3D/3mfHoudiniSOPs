/*
 *
 * Copyright (c) 2026 Palace3D
 * Extensions for 3MF import/export
 *
 * Based on SideFX HDK samples:
 *
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
 * The Save3mf SOP.  This SOP saves a geometry to the filesystem in the 3mf format.
 * We try to adhere to the 3mf Core Specification and most of the 3mf Materials
 * Specification: https://github.com/3mfconsortium
 */

#include <GU/GU_Detail.h>
#include <OP/OP_Operator.h>
#include <OP/OP_Director.h>
#include <OP/OP_AutoLockInputs.h>
#include <OP/OP_OperatorTable.h>
#include <PRM/PRM_Include.h>
#include <UT/UT_DSOVersion.h>
#include <UT/UT_Matrix3.h>
#include <UT/UT_Matrix4.h>
#include <UT/UT_JSONParser.h>
#include <GA/GA_AIFJSON.h>
#include <GA/GA_Handle.h>
#include <SYS/SYS_Math.h>
//#include <stddef.h>
#include <cstddef>
#include <string>
//#include <format> // This version of C++ is too old to use this, alas.
#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <algorithm> // for std::transform
#include <cctype> // for std::tolower
#include <chrono>
#include <ctime>
#include <SOP/SOP_Node.h>
#include <UT/UT_Console.h>
#include <UT/UT_JSONParser.h>
#include <UT/UT_JSONValue.h>
#include <GEO/GEO_Primitive.h>
#include <FS/FS_Writer.h>
//#include <UT/UT_Error.h>
#include <filesystem>
#include <FS/FS_Reader.h>
#include <sys/stat.h>
#include <minizip/zip.h>
#include <minizip/ioapi.h>
#include <zlib.h> 
//#include <boost/uuid/uuid.hpp>
//#include <boost/uuid/uuid_generators.hpp>
//#include <boost/uuid/uuid_io.hpp>
#include <random>
#include <array>
#include "SOP_Save3mf.h"

namespace fs = std::filesystem;
using namespace HDK_Sample;

// Provide entry point for installing this SOP.
void
newSopOperator(OP_OperatorTable *table)
{
    table->addOperator(new OP_Operator(
        "hdk_save3mf",
        "Save3mf",
        SOP_Save3mf::myConstructor,
        SOP_Save3mf::myTemplateList,
        1,
        1,
        //OP_Node::Type::SOP));
        0)); // What is this last param? In Flatten it is set to NULL, but what does it do?
}

// SOP parameter names.
static PRM_Name names[] = {
    PRM_Name("flip", "Flip Normals"),   // Houdini has the opposite winding order
    PRM_Name("usedShaderOverride", "Uses Shader Override"), // Uses one shader for many textures
    PRM_Name("debug", "Debug"),         // Some extra info printed to std::clog
    PRM_Name("timer", "Timer"),         // Set timer to record how long it takes
    PRM_Name("debugfiles", "Debug Files"),  // Leave tmp files available for debugging
    PRM_Name("mesh", "Mesh Name"),      // Name your mesh if you'd like
    PRM_Name("filename", "File Name"),  // 3mf file name
    PRM_Name("title", "Title (optional)"), // 3mf header option
    PRM_Name("description", "Description (optional)"), // 3mf header option
    PRM_Name("designer", "Designer (optional)"),   // 3mf header option
    PRM_Name("saveBtn", "Save"),        // Save it
};

// SOP parameter defaults
static PRM_Default  flipit(1);  // Houdini winds in opposite order of 3mf
static PRM_Default  usedOverride(0); // For now assume we're using a part imported via Read3mf()
static PRM_Default  debugit(0);
static PRM_Default  timeit(0);
static PRM_Default  debugfilesn(0);
static PRM_Default  meshn(0, "my-mesh");
static PRM_Default  filen(0, "my-model.3mf");
static PRM_Default  titlen(0, "Model Saved from Houdini");
static PRM_Default  descriptionn(0, "");
static PRM_Default  designern(0, "");

// SOP parameter template
// type, vector size, nameptr, defaultsptr, choiceptr, rangeptr, callbackfunc, ...
// To set help text, though, you need the spareptr to be set to 0 and the the paramgroup set to 1.
PRM_Template
SOP_Save3mf::myTemplateList[] = {
    PRM_Template(PRM_TOGGLE,	1, &names[0], &flipit, 0, 0, 0, 0, 1, "Flip Houdini normals to match 3mf normals."),
    PRM_Template(PRM_TOGGLE,    1, &names[1], &usedOverride, 0, 0, 0, 0, 1, "Uses a single shader for many textures."),
    PRM_Template(PRM_Type(PRM_TOGGLE) | PRM_TYPE_INVISIBLE,    1, &names[2], &debugit, 0, 0, 0, 0, 1, "Print debug information."),
    PRM_Template(PRM_TOGGLE,    1, &names[3], &timeit, 0, 0, 0, 0, 1, "Report the time it took to save the model."),
    PRM_Template(PRM_TOGGLE,    1, &names[4], &debugfilesn, 0, 0, 0, 0, 1, "Preserve intermediate model files in your temp folder for inspection."),
    PRM_Template(PRM_STRING,	1, &names[5], &meshn, 0, 0, 0, 0, 1, "Give a name to the mesh in the 3mf file. If this parameter is empty and a detail string attribute out_mesh is set, that attribute value will be used."),
    PRM_Template(PRM_FILE_E,	1, &names[6], &filen, 0, 0, 0, 0, 1, "Name of the 3mf output file. If this parameter is empty and a detail string attribute out_filename is set, then that attribute value will be used."),
    PRM_Template(PRM_STRING,    1, &names[7], &titlen, 0, 0, 0, 0, 1, "Optional title to include in the 3mf header. If this parameter is empty and a detail string attribute out_title is set, that attribute value will be used."),
    PRM_Template(PRM_STRING,    1, &names[8], &descriptionn, 0, 0, 0, 0, 1, "Optional description to include in the 3mf header."),
    PRM_Template(PRM_STRING,    1, &names[9], &designern, 0, 0, 0, 0, 1, "Optional designer's name to include in the 3mf header."),
    PRM_Template(PRM_CALLBACK,  1, &names[10], 0, 0, 0, &SOP_Save3mf::save, 0, 1, "Save the 3mf model."),
    PRM_Template(),
};


/*
 * --------------------------------------------------------------------------------
 * UTILITY FUNCTIONS
 * --------------------------------------------------------------------------------
 */


//
// Generate a pseudo-UUID string using standard C++ randomization since we cannot use Boost
// or C++20 <uuid>. This generates 16 random bytes and formats them as a standard 8-4-4-4-12
// hexadecimal UUID string. I'm hoping this is "unique enough."
//
std::string generatePseudoUUID() {
    // Initialize a Mersenne Twister engine seeded by a hardware source
    std::random_device rd;
    std::mt19937 generator(rd());
    // Get 16 bytes of random data (128 bits)
    std::array<uint8_t, 16> random_bytes;
    for (uint8_t& byte : random_bytes) {
        byte = static_cast<uint8_t>(generator() & 0xFF);
    }
    std::stringstream st;
    st << std::hex << std::uppercase << std::setfill('0');
    // 8 hex chars (4 bytes)
    for (int i = 0; i < 4; ++i) {
        st << std::setw(2) << static_cast<int>(random_bytes[i]);
    }
    st << '-';
    // 4 hex chars (2 bytes)
    for (int i = 4; i < 6; ++i) {
        st << std::setw(2) << static_cast<int>(random_bytes[i]);
    }
    st << '-';
    // 4 hex chars (2 bytes)
    for (int i = 6; i < 8; ++i) {
        st << std::setw(2) << static_cast<int>(random_bytes[i]);
    }
    st << '-';
    // 4 hex chars (2 bytes)
    for (int i = 8; i < 10; ++i) {
        st << std::setw(2) << static_cast<int>(random_bytes[i]);
    }
    st << '-';
    // 12 hex chars (6 bytes)
    for (int i = 10; i < 16; ++i) {
        st << std::setw(2) << static_cast<int>(random_bytes[i]);
    }
    return st.str();
}


//
// Convert color in floats to a string.
//
std::string
convertColor(std::array<float, 3> CdFloat) {
    std::array<int, 3> CdInt;
    for (size_t i = 0; i < CdFloat.size(); ++i) {
        float tmpF = CdFloat[i] * 255.0f + 0.5f;
        int tmpI = static_cast<int>(tmpF);
        CdInt[i] = std::min(255, std::max(0, tmpI));
    }
    {
        std::stringstream st;
        st << std::hex << std::uppercase << std::setfill('0');
        for (int component : CdInt) {
            st << std::setw(2) << component;
        }
        return st.str();
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
// Get a timestamp accurate enough for millisecond measurements.
//
//std::chrono::time_point<std::chrono::system_clock>
auto
generateTimestamp() {
    auto now = std::chrono::high_resolution_clock::now();
    return now;
}


//
// Get a time string to use for filename uniqueness and putting in 3mf headers. Include milliseconds
// which complicates things here a bit.
//
std::string
generateTimestampString() {
    using namespace std::chrono;

    // Get time in a way that can be formatted as a string
    auto now = system_clock::now();
    // Total duration since the epoch for this time stamp
    auto duration_since_epoch = now.time_since_epoch();
    // Turn this into milliseconds since epoch
    auto ms_total = duration_cast<milliseconds>(duration_since_epoch).count();
    // Total seconds since epoch
    std::time_t tt = system_clock::to_time_t(now);
    // Fractional milliseconds remaining after removing full seconds (tt is in xeconds)
    long ms_remainder = ms_total - (static_cast<long>(tt) * 1000);
    // For clean clocks, this should always be true, but... Make sure remainder is between 0 and 999
    ms_remainder = std::max(0L, ms_remainder);
    ms_remainder = std::min(999L, ms_remainder);
    // localtime is non-reentrant
    std::tm tm = *std::localtime(&tt);
    std::stringstream st;
    //st << std::put_time(&tm, "%Y-%m-%d_%H:%M:%S");
    st << std::put_time(&tm, "%Y-%m-%d_%H-%M-%S"); // Cannot use : for compatibility on Windows systems
    // Append milliseconds
    //st << ":";
    st << "-"; // No : for compatibility on windows systems
    st << std::setfill('0') << std::setw(3) << ms_remainder;

    return st.str();
}


//
// Clear and reset class data structures.
//
SOP_Save3mf::ErrorCode
SOP_Save3mf::clearData() {
    LOG_DEBUG(this->debug, "Entering clearData.");

    this->resourceId = INITIAL_ID; //MUST be > 1 so we can be use a lesser value for our object id
    this->objectId = INITIAL_ID - 1;
    this->colorgroupId = -1;
    this->primColorTextureGroupId = -1;
    this->hasFileTexture = false;
    this->colorType = ColorTypes::NONE;
    this->defaultColorResource = -1;
    this->primTexts.clear();
    this->primTextGroup.clear();
    this->primTextDict.clear();
    this->primTextBaseDict.clear();
    this->primTextRewriteDict.clear();
    this->primColorDict.clear();
    this->colorsByGroup.clear();
    this->primTextCoords.clear();
    this->files_to_add.clear();
    this->modelOutput.clear();

    return ErrorCode::SUCCESS;
}


//
// Create a copy of a string but in lower case
//
std::string
return_lower(std::string s /* a copy of the the string parameter */) {
    // std::transform applies a function (std::tolower) to a range of elements.
    // s.begin() and s.end() define the range (the entire string).
    // The result is written back to s.begin() (modifies the string in place).
    std::transform(s.begin(), s.end(), s.begin(),
        // Use a lambda function to wrap std::tolower
        // This is often needed to correctly handle overloads for std::tolower
        [](unsigned char c){ return std::tolower(c); }
    );
    return s;
}


//
// Clean up the tmp files. Don't report errors if we can't remove the files. They waste space, but
// the folder is timestamped so they won't conflict.
//
void
cleanupFiles(std::string tmpDir, bool leaveFiles) {
    
    std::error_code ec;

    if (leaveFiles) {
        return;
    }
    // This removes the directory and all its contents recursively.
    uintmax_t count = std::filesystem::remove_all(tmpDir, ec);
    /*
    if (ec) {
        // This catches permissions errors, or if the initial path didn't exist.
        std::cerr << "Error removing directory " << dirpath 
                  << " and contents: " << ec.message() << std::endl;
    }
    */
 }


//
// Read a file into a string. (Minizip doesn't read files directly.)
//
std::string
readFileToString(const std::string& filePath) {
    std::ifstream file(filePath, std::ios::in | std::ios::binary);
    if (!file) {
        std::cerr << "Error: Could not open source file: " << filePath << std::endl;
        return "";
    }
    // Read the file content efficiently
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}


//
// For debugging, print the primTextCoords dictionary.
//
void
printPrimTextCoords(const std::unordered_map<int, std::array<int, 3>>& primTextCoords, bool debug) {
    if (primTextCoords.empty()) {
        LOG_DEBUG(debug, "primTextCoords is empty.");
        return;
    }
    {
        std::stringstream st;
        st << "--- primTextCoords Contents (Total Primitives: ";
        st << primTextCoords.size() << ") ---" << std::endl;

        for (const auto& pair : primTextCoords) {
            const int& key = pair.first;
            const std::array<int, 3>& coords = pair.second;

            st << "Primitive [" << std::setw(3) << key << "]: { ";
            for (const int& index : coords) {
                st << std::setw(4) << index << " ";
            }
            st << "}" << std::endl;
        }
        st << "-------------------------------------------------" << std::endl;
        LOG_DEBUG(debug, st.str());
    }
    return;
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
SOP_Save3mf::myConstructor(OP_Network *net, const char *name, OP_Operator *op)
{
    return new SOP_Save3mf(net, name, op);
}

// Class constructor
SOP_Save3mf::SOP_Save3mf(OP_Network *net, const char *name, OP_Operator *op)
    : SOP_Node(net, name, op), // Base class call
      saveGeometry(false)     // Inititalize std::atomic member
{
    // This SOP does not manually manage its data IDs
    mySopFlags.setManagesDataIDs(false);
}


//
// Class destructor
//
SOP_Save3mf::~SOP_Save3mf() {}


//
// Cooking SOP. The first time this is put down, nothing interesting happens. But a save callback will cause
// this sop to cook again, with a flag set to make it actually save the geometry.
//
OP_ERROR
SOP_Save3mf::cookMySop(OP_Context &context)
{
    fpreal t = context.getTime();
    UT_Console::initConsole();
    
    // We must lock our inputs before we try to access their geometry.
    // OP_AutoLockInputs will automatically unlock our inputs when we return.
    // NOTE: Don't call unlockInputs yourself when using this!
    OP_AutoLockInputs inputs(this);
    if (inputs.lock(context) >= UT_ERROR_ABORT) {
        return error();
    }
        
    int input_changed;
    duplicateChangedSource(/*input*/0, context, &input_changed);
    
    // Set up parameters. We at least need the debug for logging.
    this->debug = this->DEBUG(t);
    this->debugfiles = this->DEBUGFILES(t);
    this->flip = this->FLIP(t);
    this->usedShaderOverride = this->OVERRIDE(t);
    this->timer = this->TIMER(t);
    this->MESH(this->mesh, t);
    this->FILENAME(this->filename, t);
    this->TITLE(this->title, t);
    this->DESCRIPTION(this->description, t);
    this->DESIGNER(this->designer, t);

    LOG_DEBUG(this->debug, "Entering cookMySop");

    // If the incoming geometry carries detail attributes named "out_meshname"
    // or "out_filename" or "out_title", and if the associated parameters are empty,
    // then use the attributes for the parameter values. This lets
    // upstream nodes (e.g. a wrangle building a name from other parameters)
    // drive these fields dynamically without needing an expression that would
    // re-cook this node's own (already-locked) input chain.
    GA_ROHandleS filename_attr(gdp, GA_ATTRIB_DETAIL, "out_filename");
    if (filename_attr.isValid()) {
        if (!this->filename.empty()) {
            LOG_DEBUG(this->debug, "Filename attr is valid but filename parameter is set.");
            addWarning(SOP_MESSAGE, "File Name parameter is set, but an "
                "'out_filename' detail attribute is also present on the input "
                "geometry; the parameter value will be used instead of the attribute.");
        } else {
            this->filename = filename_attr.get(GA_Offset(0)).toStdString();
            LOG_DEBUG(this->debug, "Using filename from detail attribute: " << this->filename);
        }
    }

    GA_ROHandleS mesh_attr(gdp, GA_ATTRIB_DETAIL, "out_meshname");
    if (mesh_attr.isValid()) {
        if (!this->mesh.empty()) {
            LOG_DEBUG(this->debug, "Mesh attr is valid but mesh parameter is set.");
            addWarning(SOP_MESSAGE, "Mesh Name parameter is set, but an "
                "'out_meshname' detail attribute is also present on the input "
                "geometry; the parameter value will be used instead of the attribute.");
        } else {
            this->mesh = mesh_attr.get(GA_Offset(0)).toStdString();
            LOG_DEBUG(this->debug, "Using mesh name from detail attribute: " << this->mesh);
        }
    }

    GA_ROHandleS title_attr(gdp, GA_ATTRIB_DETAIL, "out_title");
    if (title_attr.isValid()) {
        if (!this->title.empty()) {
            LOG_DEBUG(this->debug, "Title attr is valid but title parameter is set.");
            addWarning(SOP_MESSAGE, "Title parameter is set, but an "
                "'out_title' detail attribute is also present on the input "
                "geometry; the parameter value will be used instead of the attribute.");
        } else {
            this->title = title_attr.get(GA_Offset(0)).toStdString();
            LOG_DEBUG(this->debug, "Using title from detail attribute: " << this->title);
        }
    }    

    this->start = generateTimestamp();
    this->timeString = generateTimestampString();
    this->t = t;

    // Atomically read the current state and set the flag to false (reset)
    // so we don't have two threads possibly trying to save the geometry
    // at the same time.
    bool shouldSave = this->saveGeometry.exchange(false); 

    if (!shouldSave) {
        // If we shouldn't save a geometry, the function exits, returning the cached geometry.
        LOG_DEBUG(this->debug, "Exiting cookMySop with no geometry to cook");
        return OP_ERROR::UT_ERROR_NONE;
    }
    LOG_DEBUG(this->debug, "Should save is " + std::to_string(shouldSave));
    LOG_DEBUG(this->debug, "We need to save the geometry into a 3mf file.");

    // Clear out data structures so they don't have crud from the last save.
    clearData();

    // Check if there's anything to save
    const OP_Node *const_input = this->getInput(0);
    if (!const_input) {
        std::cerr << "Error: Missing input." << std::endl;
        this->addError(UT_ERROR_FATAL, "No geometry input found.");
        return this->error();
    }

    // Set up our internal error reporting.
    SOP_Save3mf::ErrorCode myError = SOP_Save3mf::ErrorCode::SUCCESS;

    // We use the 3mf suffix. Check that the file name in the parameters is vaguely sensible.
    const std::string suffix = ".3mf";
    if (this->filename.empty()) {
        std::cerr << "Error: no filename was provided for output." << std::endl;
        return this->error();  // I don't think this can be the right kind of return value. Fix XXX
    }
    if (this->filename.length() < suffix.length() || (this->filename.rfind(suffix) != this->filename.length() - suffix.length())) {
        this->filename += suffix;
    }

    if ((myError = this->buildModel(this->gdp)) != SOP_Save3mf::ErrorCode::SUCCESS) {
        std::cerr << "Error: Unable to build 3mf model from geometry. Error code was " << std::to_string(static_cast<int>(myError)) << std::endl;
        return this->error(); // Fix to the right kind of return value XXX
    }

    // I think this is bogus at this point. We'll already have aborted if something has gone
    // wrong in our buildModel() function.
    if (this->error() >= UT_ERROR_ABORT) {
        return this->error(); // Same here XXX
    }

    // We've built the model (as an output string). Now save it to the file system.
    if ((myError = this->doOutput()) != SOP_Save3mf::ErrorCode::SUCCESS) {
        std::cerr << "Error: Unable to finish writing model to file. Error code was " << std::to_string(static_cast<int>(myError)) << std::endl;
        return this->error(); // Same here XXX
    }

    auto endTime = generateTimestamp();
    auto durationTime = endTime - this->start;
    auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(durationTime);

    LOG_DEBUG(this->timer || this->debug, "Exiting read -- took " + std::to_string(duration_ms.count()) + " milliseconds");
    LOG_DEBUG(this->debug, "\nCompleted cooking with no errors. Mesh is " + this->mesh + " and filename is " + this->filename);
    
    return error();
}


//
// This is the callback from a button push to save a model as a 3mf file.
//
// Note that a callback requires a return value of 0 to indicate failure and 1 for success.
//
// In this callback we set flags to cause cookMySop to run and to recognize that this time it
// actually has to do something (i.e. save the geometry). All the work
// is essentially done by cookMySop.
//
/*static*/ int
SOP_Save3mf::save(void *data, int index, fpreal t, const PRM_Template *tplate)
{
    // Get the node for this instance of the callback
    SOP_Save3mf *this_node = static_cast<SOP_Save3mf*>(data);

    if (this_node == nullptr) {
        std::cerr << "Error: Unable to retrieve node for this callback" << std::endl;
        return 0;
    }

    LOG_DEBUG(this_node->debug, "\nEntering save");

    this_node->saveGeometry = true; // So cookMySop knows to save the geometry.
    this_node->forceRecook(); // Cause node to cook again
    LOG_DEBUG(this_node->debug, "Save geometry is " + std::to_string(this_node->saveGeometry));

    LOG_DEBUG(this_node->debug, "Exiting save");

    return 1;
}


//
// Build the 3mf model from the Houdini geometry. To do this we run through the geometry and
// put the information in 3mf format into a long string, which we'll later push to the model.model
// file.
// Note that we're currently saying the entire mesh is a single object for the purposes of
// having a single objectId. This is a limitation we'll eventually address.
//
SOP_Save3mf::ErrorCode
SOP_Save3mf::buildModel(const GU_Detail* gdp) {
    
    SOP_Save3mf::ErrorCode myError = SOP_Save3mf::ErrorCode::SUCCESS;
    GA_Attribute *attrib;

    // Set up the remaining header information and push it to the string with writeHeader().
    //std::string title = "Model Saved from Houdini: ";
    //title.append(this->mesh);
    // So far we implement the material extension but not the boolean extension.
    // The production extension seems to be necessary for the UUIDs the spec requests.
    // writeHeader() initiates the model info pushed to our 3mf string modelOutput with
    // the <model> keyword.
    this->writeHeader(true, false, true );

    // The beginning of the actual model section in 3mf.
    this->modelOutput.append("<resources>\n");

    // We go through the model gathering texture information so that it can be written to
    // the front part of the 3mf file. We check for textures before colors in
    // case of base color textures, which we just treat as colors.
    if ((myError = this->saveTextures(gdp)) != SOP_Save3mf::ErrorCode::SUCCESS) {
        std::cerr << "Error: Unable to gather model texture information. Error code was "
            << std::to_string(static_cast<int>(myError)) << std::endl;
        return ErrorCode::BAD_GEO;
    }
    // To print the dictionary for debugging, change false to this->degbug
    //printPrimTextCoords(this->primTextCoords, false);

    // Now gather color information.
    if ((myError = this->saveColors(gdp)) != SOP_Save3mf::ErrorCode::SUCCESS) {
        std::cerr << "Error: Unable to gather model color information. Error code was "
            << std::to_string(static_cast<int>(myError)) << std::endl;
        return ErrorCode::BAD_GEO;
    }

    // Create a default color group and color if needed.
    this->defaultColor(gdp);

    LOG_DEBUG(this->debug, "Writing mesh to " + this->mesh);
    
    // Now we collect the actual geometry information.
    if (this->write3mfObjectMesh(gdp) != ErrorCode::SUCCESS) {
        std::cerr << "Error: Unable to write out object mesh." << std::endl;
        return ErrorCode::BAD_GEO;
    }
    
    // End of the model information
    this->modelOutput.append("</resources>\n");

    // Write the build items -- build transforms, etc.
    LOG_DEBUG(this->debug, "Writing build items");

    std::string uuid_str = generatePseudoUUID();
    this->modelOutput.append("<build p:UUID=\"" + uuid_str + "\">\n");
    // Put the item in the list of build items since the whole object needs to go
    // there or it will be ignored when read back in.
    if (this->write3mfItem(gdp, this->objectId) != ErrorCode::SUCCESS) {
        std::cerr << "Error: Unable to write out object mesh." << std::endl;
        return ErrorCode::BAD_GEO;
    }
    
    // End of the build info and the whole model string.
    this->modelOutput.append("</build>\n");
    this->modelOutput.append("</model>\n");

    return ErrorCode::SUCCESS;
}


//
// Gather the actual geometry to write out as vertices and triangles. This is
// also where properties are assigned to triangles, since that's part of the mesh info.
// This all gets appended to the main modelOutput string. If a face doesn't have color or
// texture, we assign to it the default object-level property which I've chosen to have be a
// colorgroup (and not something else like a texture). We also do that if the model uses
// detail color. Doing it this way takes up more space in the output 3mf file -- we could
// avoid writing out the color for a triangle, for instance, and just let it default to
// the object-level property. But I've seen some software not handle that well,
// so I waste space and try to make it very clear here. We just use a "myId" of 1, as we
// do with write3mfItem().
//
// The flow in this routine gives texture priority over color if a triangle has both.
// We do not attempt to do any blending of properties as seen in Houdini (yet).
//
// Since we're currently outputting everything as one object, there will be only one object. 
// Since we're using an initial resource ID > 1 (see header file), we can safely assign
// the value in the header file to this object id.
//
// Note that what 3mf calls vertices are Houdini points.
//
SOP_Save3mf::ErrorCode
SOP_Save3mf::write3mfObjectMesh(const GU_Detail* gdp) {
    LOG_DEBUG(this->debug, "Entering write3mfObjectMesh");
    if (gdp == nullptr) {
        LOG_DEBUG(this->debug, "fooey");
    }
    //printPrimTextCoords(this->primTextCoords, this->debug);

    // First add the object string with default property info
    std::string uuid_str = generatePseudoUUID();
    this->modelOutput.append("<object id=\"" + std::to_string(this->objectId));
    this->modelOutput.append("\" name=\"" + this->mesh + "\" type=\"model\" pid=\"");
    this->modelOutput.append(std::to_string(this->defaultColorResource));
    this->modelOutput.append("\" pindex=\"0\" p:UUID=\"" + uuid_str + "\">\n");

    this->modelOutput.append(" <mesh>\n");

    // write all the vertices (that's in 3mf lingo. They are points in Houdini lingo)
    this->modelOutput.append("  <vertices>\n");

    GA_Size numPts = gdp->getNumPoints();
    LOG_DEBUG(this->debug, "There are " + std::to_string(numPts) + " vertices");
    //LOG_DEBUG(true, "There are " + std::to_string(numPts) + " vertices");
    
    for (GA_Iterator it(gdp->getPointRange()); !it.atEnd(); ++it) {
        GA_Offset ptOffset = *it;
        UT_Vector3 pos = gdp->getPos3(ptOffset);
        {
            std::stringstream st;
            st << "    <vertex x=\"" << std::setw(3) << pos[0] << "\"";
            st << " y=\"" << std::setw(3) << pos[1] << "\"";
            //st << " z=\"" << std::setw(3) << pos[2] << "\"/>\n";
            st << " z=\"" << std::setw(3) << pos[2] << "\"/>\n";
            //st << std::hex << std::uppercase << std::setfill('0');
            this->modelOutput.append(st.str());
        }
    }

    this->modelOutput.append("  </vertices>\n");

    // write all the triangles
    GA_Size numPrims = gdp->getNumPrimitives();
    LOG_DEBUG(this->debug, "In write we have " << numPrims << " primitives.");
    //LOG_DEBUG(true, "In write we have " << numPrims << " primitives.");
    this->modelOutput.append("  <triangles>\n");

    //printPrimTextCoords(this->primTextCoords, this->debug);
    for (GA_Iterator it(gdp->getPrimitiveRange()); !it.atEnd(); ++it) {
      GA_Offset primOffset = *it;
      const GA_Primitive *primPtr = gdp->getPrimitive(primOffset);
      GA_Index primIndex = gdp->primitiveIndex(primOffset);

        std::stringstream st;
        LOG_DEBUG(false, "We're looking at prim with index " + std::to_string(primIndex));

        GA_Size numVerts = primPtr->getVertexCount();
        if (numVerts != 3) {
            LOG_DEBUG(this->debug, "Error: Primitives MUST be triangles.");
            std::cerr << "Error: Primitives MUST be triangles." << std::endl;
            return ErrorCode::BAD_GEO;
        }
        LOG_DEBUG(false, std::string("We got ") + std::to_string(numVerts) + std::string(" vertices for this prim"));
        const GA_OffsetListRef vertices = gdp->getPrimitiveVertexList(it.getOffset());
        //LOG_DEBUG(this->debug, "Another way to see it -- we have " + std::to_string(vertices.size()) + " vertices in this prim");
        std::array<int, 3> myPoints;
        myPoints[1] = primPtr->getPointIndex(1);
        if (this->flip) {
            myPoints[0] = primPtr->getPointIndex(2);
            myPoints[2] = primPtr->getPointIndex(0);
        } else {
            myPoints[0] = primPtr->getPointIndex(0);
            myPoints[2] = primPtr->getPointIndex(2);
        }
        LOG_DEBUG(false, "    My points in order are " + std::to_string(myPoints[0]) + ", "
            + std::to_string(myPoints[1]) + ", " + std::to_string(myPoints[2]));
        
        // Does this primitive have texture?
        if (this->hasFileTexture && this->primTexts[primIndex] >= 0) {
            int textGroupId = this->primTextGroup[primIndex];
            //LOG_DEBUG(this->debug, std::string("For PRIM ") + std::to_string(primIndex) + std::string(" with textGroupID ")
                //+ std::to_string(textGroupId) + std::string(" we have indices ")
                //+ std::to_string(primTextCoords[primIndex][0]) + std::string(" ")
                //+ std::to_string(primTextCoords[primIndex][1]) + std::string(" ")
                //+ std::to_string(primTextCoords[primIndex][2]));
            std::array<int, 3> textCoordIndices = this->primTextCoords[primIndex];
            st << "    <triangle v1=\"" << std::to_string(myPoints[0]) << "\" v2=\""
                << std::to_string(myPoints[1]) + "\" v3=\"" << std::to_string(myPoints[2])
                << "\" pid=\"" << std::to_string(textGroupId)
                << "\" p1=\"" << std::to_string(textCoordIndices[0])
                << "\" p2=\"" << std::to_string(textCoordIndices[1])
                << "\" p3=\"" << std::to_string(textCoordIndices[2])
                << "\"/>\n";
        // Check if the prim number is in the primColorDict. Set this up with the base color
        // texture group.
        } else if (auto pt = this->primColorDict.find(primIndex);
            pt != this->primColorDict.end()) {
            int index = pt->second.index;
            st << "    <triangle v1=\"" << std::to_string(myPoints[0]) << "\" v2=\""
                << std::to_string(myPoints[1]) + "\" v3=\"" << std::to_string(myPoints[2])
                << "\" pid=\"" << std::to_string(this->primColorTextureGroupId)
                << "\" p1=\"" << std::to_string(index) << "\"/>\n";
        } else {
            // Remember that in 3mf, points and vertices are the same thing
            switch (this->colorType) {
            // Using point color in Houdini. There will be one color per point in the color group.
            case ColorTypes::POINT:
                {
                    int i1 = getColorIndex(this->colorsByGroup[this->colorgroupId], primIndex, 0);
                    int i2 = getColorIndex(this->colorsByGroup[this->colorgroupId], primIndex, 1);
                    int i3 = getColorIndex(this->colorsByGroup[this->colorgroupId], primIndex, 2);
                    if (this->flip) {
                        int tmp = i1;
                        i1 = i3;
                        i3 = tmp;
                    }
                    if (i1 == -1 || i2 == -1 || i3 == -1) {
                        std::cerr << "Error: No recorded color for a point with color." << std::endl;
                        return ErrorCode::BAD_GEO;
                    }
                    LOG_DEBUG(this->debug, "Got colorgroup indices " << i1 << ", " << i2 << ", and " << i3);
                    st << "    <triangle v1=\"" << std::to_string(myPoints[0]) << "\" v2=\""
                    << std::to_string(myPoints[1]) + "\" v3=\""
                    << std::to_string(myPoints[2]) << "\" pid=\""
                    << std::to_string(this->colorgroupId)
                    << "\" p1=\"" << std::to_string(i1) << "\" p2=\""
                    << std::to_string(i2) << "\" p3=\"" << std::to_string(i3) << "\"/>\n";
                    break;
                }
            // Using vertex color. There will be one color per vertex in the color group.
            case ColorTypes::VERTEX:
                {
                    int i1 = getColorIndex(this->colorsByGroup[this->colorgroupId], primIndex, 0);
                    int i2 = getColorIndex(this->colorsByGroup[this->colorgroupId], primIndex, 1);
                    int i3 = getColorIndex(this->colorsByGroup[this->colorgroupId], primIndex, 2);
                    if (this->flip) {
                        int tmp = i1;
                        i1 = i3;
                        i3 = tmp;
                    }
                    if (i1 == -1 || i2 == -1 || i3 == -1) {
                        std::cerr << "Error: No recorded color for a point with color." << std::endl;
                        return ErrorCode::BAD_GEO;
                    }
                    LOG_DEBUG(this->debug, "Got colorgroup indices " << i1 << ", " << i2 << ", and " << i3);
                    st << "    <triangle v1=\"" << std::to_string(myPoints[0]) << "\" v2=\"" <<
                    std::to_string(myPoints[1]) + "\" v3=\""
                    << std::to_string(myPoints[2]) << "\" pid=\""
                    << std::to_string(this->colorgroupId)
                    << "\" p1=\"" << std::to_string(i1) << "\" p2=\""
                    << std::to_string(i2) << "\" p3=\"" << std::to_string(i3) << "\"/>\n";
                    break;
                }
            // Using prim color. There will be one color per prim in the color group.
            case ColorTypes::PRIM:
                {
                    int i1 = getColorIndex(this->colorsByGroup[this->colorgroupId], primIndex, 0);
                    st << "    <triangle v1=\"" << std::to_string(myPoints[0]) << "\" v2=\""
                    << std::to_string(myPoints[1]) + "\" v3=\"" << std::to_string(myPoints[2])
                    << "\" pid=\"" << std::to_string(this->colorgroupId)
                    << "\" p1=\"" << std::to_string(i1) << "\"/>\n";
                    break;
                }
            // We treat detail color and no color the same way by assigning a default
            // object-level color to the model.
            case ColorTypes::DETAIL:
            case ColorTypes::NONE:
                {
                    st << "    <triangle v1=\"" << std::to_string(myPoints[0]) << "\" v2=\""
                    << std::to_string(myPoints[1]) + "\" v3=\"" << std::to_string(myPoints[2])
                    << "\" pid=\"" << std::to_string(this->defaultColorResource)
                    << "\" p1=\"" << std::to_string(0) << "\"/>\n";
                    break;
                }
            }
        }
        this->modelOutput.append(st.str());        
    }

    this->modelOutput.append("  </triangles>\n");
    this->modelOutput.append(" </mesh>\n");
    this->modelOutput.append(" </object>\n");

    LOG_DEBUG(this->debug, "Exiting write3mfObjectMesh");
    return ErrorCode::SUCCESS;
}


//
// Add this item to the list of items to output. Since we're currently outputting
// everything as one object, there will be only one item in the list.
//
SOP_Save3mf::ErrorCode
SOP_Save3mf::write3mfItem(const GU_Detail* gdp, int objectId) {

    LOG_DEBUG(this->debug, "Entering write3mfItem");
    std::string uuid_str = generatePseudoUUID();
    this->modelOutput.append(" <item objectid=\"" + std::to_string(objectId));
    this->modelOutput.append("\" transform=\"" + SOP_Save3mf::TRANSFORM_TRIVIAL
        + "\" p:UUID=\"" + uuid_str + "\"/>\n");

    LOG_DEBUG(this->debug, "Exiting write3mfItem");
    return ErrorCode::SUCCESS;
}


//
// Go through the part and find the material assignments for all the primitives/vertices.
// Currently, even if the textures are repeated, I list a tex2coord for each vertex of each primitive.
// This should be optimized.
//
SOP_Save3mf::ErrorCode
SOP_Save3mf::saveTextures(const GU_Detail* gdp) {

    LOG_DEBUG(this->debug, "In saveTextures");

    // Go through prims and enter distinct textures in a key-value database.
    // Record which prims have which texture.
    // For each of those prims, find the vertices and their uv coordinates and which texture they have.
    // Then for the first texture, write to a string the vertices using it, repeat for other textures.
    // This way, when we get to writing the triangles, we know which texture group a triangle uses and
    // the tex2coord for each vertex.

    // Does the part have any material path?
    GA_ROHandleS shaderpath_h(gdp, GA_ATTRIB_PRIMITIVE, "shop_materialpath");
    if (!shaderpath_h.isValid()) {
        LOG_DEBUG(this->debug, "No texture on model.");
        return ErrorCode::SUCCESS;
    }
    GA_ROHandleS override_h(gdp, GA_ATTRIB_PRIMITIVE, "material_override");
    if (this->usedShaderOverride && !override_h.isValid()) {
        LOG_DEBUG(this->debug, "No override attribute for a textured object imported from read3mf()");
        return ErrorCode::BAD_GEO;
    }
    LOG_DEBUG(this->debug, "We have file-based or base color texture on this part, at least somewhere.");

    // Find the material path for each prim
    int thisTextureId = -1; // -1 cannot be a resourceId, so we can initialize with it
    UT_String shaderpath;
    for (GA_Iterator it(gdp->getPrimitiveRange()); !it.atEnd(); ++it) {
        GA_Offset offset = *it;
        int primNum = it.getIndex();
        
        LOG_DEBUG(this->debug, "PRIM " << primNum << ":");
        shaderpath = shaderpath_h.get(offset);
        std::string shaderstring(shaderpath.buffer());
        LOG_DEBUG(this->debug, "shader string is " << shaderpath.buffer());
        // Note that we seem to get a shader path even if there is no texture on this primitive!
        // We have to check if the shader node is valid
        OP_Node*   shaderNode = nullptr;
        shaderNode = OPgetDirector()->findNode(shaderstring.c_str());  // find via absolute path
        if (shaderNode == nullptr) {
            // No texture or base color texture on this primitive
            // this->primTexts[it.getIndex()] = TextureTypes::NONE;
            this->primTexts.push_back(-1);
            LOG_DEBUG(this->debug, "    No texture on this primitive.");
            continue;
        }
        
        // Check which type of texture -- returns 0 or 1 (false or true)
        int useTexture = shaderNode->evalInt("basecolor_useTexture", /* component index */ 0, this->t);
        if (useTexture == 0) {
            // This means we're using the base color as a texture.
            //this->primTexts.push_back(thisTextureId);
            this->primTexts.push_back(-1);
            LOG_DEBUG(this->debug, "   Base color texture on this primitive.");
            std::array<float, 3> baseColor;
            shaderNode->evalFloats("basecolor", baseColor.data(), this->t);
            // Set up primColorDict with the color of this non-file texture
            {
                std::stringstream st;
                st << "    Red: " << baseColor[0] << ", Green: " << baseColor[1] << ", Blue: " << baseColor[2];
                //LOG_DEBUG(this->debug, st.str());
                this->primColorDict[it.getIndex()].color = baseColor;
                this->primColorDict[it.getIndex()].index =
                    this->primColorDict.size() - 1;
            }
            continue;
        }
        // This primitive uses file-based texture. Get the texture path
        UT_String textureFileParam;
        shaderNode->evalString(textureFileParam, "basecolor_texture", /* component index */ 0, this->t);
        LOG_DEBUG(this->debug, "textureFilePath will be " << textureFileParam.buffer());
        UT_StringHolder textureFilePath = textureFileParam.buffer();

        // Check for emptiness
        if (textureFilePath.empty() && !this->usedShaderOverride) {
            std::cerr << "Error: No texture file for this textured primitive." << std::endl;
            return ErrorCode::BAD_GEO;
        } else if (textureFilePath.empty()) { // We look for the texture path in the material_override attribute
            LOG_DEBUG(this->debug, "empty textureFilePath, so checking for override");
            // Assuming 'prim' is a GA_Primitive pointer
            //GA_ROHandleS override_h(gdp->findPrimitiveAttribute("material_override"));
            // 1. Outside the loop (only do this once for performance!)
            // Use the String Handle since Dictionaries are stored as JSON strings

            /*
            const GA_Attribute *found_attr = gdp->findPrimitiveAttribute("override_h");
            if (found_attr) {
                // getStorageClass() returns an enum like GA_STORECLASS_STRING, GA_STORECLASS_DICT, etc.
                LOG_DEBUG(this->debug, "Attribute exists! Storage Class: " << (int)found_attr->getStorageClass());
                LOG_DEBUG(this->debug, "Type Name: " << found_attr->getType().getTypeName());
            } else {
                LOG_DEBUG(this->debug, "Attribute 'override_h' NOT found on Primitives.");
            }
            */

            GA_ROHandleS override_h(gdp, GA_ATTRIB_PRIMITIVE, "override_h"); // MOVE ME OUTSIDE the loop XXXXX
            LOG_DEBUG(this->debug, "Back from getting override_h");

            if (override_h.isValid()) {
                LOG_DEBUG(this->debug, "override_h dict is valid");
                const char *json_cstr = override_h.get(offset);

                if (json_cstr && *json_cstr) {
                    LOG_DEBUG(this->debug, "json_cstr okay");
                    UT_Options options;
                    
                    // 1. Create the stream from the raw C-string
                    UT_IStream is(json_cstr, strlen(json_cstr), UT_ISTREAM_BINARY);
                    
                    // 2. Use the 2-argument load() candidate your compiler explicitly listed:
                    // bool load(const char *filename, UT_IStream &is);
                    // We provide a dummy filename as the first argument.
                    options.load("internal_json", is); 

                    // 3. Extract your data
                    int64 use_tex = options.getOptionI("basecolor_useTexture");
                    
                    UT_String tex_path;
                    options.getOptionS("basecolor_texture", tex_path);
                    LOG_DEBUG(this->debug, "We got past getOptions");

                    // 4. Using .length() as the absolute fallback for UT_String check
                    if (use_tex > 0 && tex_path.length() > 0) {
                        LOG_DEBUG(this->debug, "Yay. use_tex is " << use_tex << " and texture is " << tex_path);
                        textureFilePath = tex_path;
                    } else {
                        LOG_DEBUG(this->debug, "yuck, use_tex is " << use_tex);
                    }
                } else {
                    LOG_DEBUG(this->debug, "no good json_cstr");
                }
            } else {
                LOG_DEBUG(this->debug, "override_h dict is not valid");
            }
/*
            if (override_h.isValid()) {
                LOG_DEBUG(this->debug, "override_h is valid");
                // Get the JSON string from the attribute
                // Use the offset directly to get the string
                UT_StringHolder json_str = override_h.get(offset);
                const char* start = json_str.buffer();
                exint len = json_str.length();
                if (len >= 2 && start[0] == '"') {
                    LOG_DEBUG(this->debug, "We're adjusting the start buffer");
                    start++;
                    len -= 2;
                } else {
                    LOG_DEBUG(this->debug, "len was " << len);
                }
                
                //UT_IStream is((const char *)json_str, json_str.length(), UT_ISTREAM_ASCII);
                UT_IStream is(start, len, UT_ISTREAM_ASCII);
                UT_Options options;
                UT_JSONParser parser;

                if (options.load(parser, false, &is, true)) { // the false means don't ignore ignore errorz
                    LOG_DEBUG(this->debug, "Load worked. Option count: " << options.entries());
                    UT_String texturePath;
                    options.getOptionS("basecolor_texture", texturePath);
                    if (texturePath.isstring()) {
                        textureFilePath = UT_StringHolder(texturePath);
                        LOG_DEBUG(this->debug, "Found it: " << textureFilePath);
                    } else {
                        LOG_DEBUG(this->debug, "Key 'basecolor_texture'not found in options");
                    }

                    // In 20.5, getOptionS returns void, so we check existence separately
                    if (options.hasOption("basecolor_texture")) {
                        LOG_DEBUG(this->debug, "There was a basecolor_texture option");
                        UT_String texturePath;
                        options.getOptionS("basecolor_texture", texturePath);
                        
                        textureFilePath = UT_StringHolder(texturePath);
                        LOG_DEBUG(this->debug, "textureFilePath is now " << textureFilePath);
                    } else {
                        LOG_DEBUG(this->debug, "No basecolor_texture key in JSON");
                    }
                } else {
                    std::cerr << "Unable to parse JSON options" << std::endl;
                    return ErrorCode::BAD_GEO;
                }

                LOG_DEBUG(this->debug, "we got here");
                
                // Map search must use std::string conversion
                // std::string searchKey = textureFilePath.toStdString(); (Safe)
                // Or use .buffer() if textureFilePath is already populated
                if (!textureFilePath.isEmpty()) {
                    std::string key = textureFilePath.toStdString();
                    if (this->primTextDict.find(key) != this->primTextDict.end()) {
                        LOG_DEBUG(this->debug, "Got textureFilePath " << textureFilePath.buffer());
                    } else { LOG_DEBUG(this->debug, "key not found");} // XXXXXX
                } else {
                    std::cerr << "textureFilePath was isEmpty" << std::endl;
                        return ErrorCode::BAD_GEO;
                }
            } else {
                std::cerr << "There is no valid override attribute handler" << std::endl;
                return ErrorCode::BAD_GEO;
            }
*/
        }
        this->hasFileTexture = true; // We have file-based texture someplace on the model
        //LOG_DEBUG(this->debug, std::string("    File texture ") + textureFilePath);
        // Check if texture already added to primTextDict. If not, add it.
        // Keys are texture paths, values are the resourceId for the texture
        std::string textureFileKey = textureFilePath.toStdString();
        if (this->primTextDict.find(textureFileKey) != this->primTextDict.end()) {
            int thisTextureId = this->primTextDict[textureFileKey];
            LOG_DEBUG(this->debug, "    We already have this texture, with resourceId " << thisTextureId);
            this->primTexts.push_back(thisTextureId);
        } else { // New texture with a new path
            // Convert texture file path to std::filesystem::path for cross-platform handling
            std::filesystem::path fsPath(textureFilePath.toStdString());
            std::string tFileStr = fsPath.filename().string();
            // Sanitize filename - replace characters that are problematic in URLs and filenames
            // This deals with the ? I was getting from opdef filenames that had ? in them as a separator
            std::replace(tFileStr.begin(), tFileStr.end(), '?', '_');
            std::replace(tFileStr.begin(), tFileStr.end(), ':', '_');
            std::replace(tFileStr.begin(), tFileStr.end(), '*', '_');
            std::replace(tFileStr.begin(), tFileStr.end(), '"', '_');
            
            std::string suffix = fsPath.extension().string();
            std::string testname = fsPath.stem().string();
            std::string textureFileKey = textureFilePath.toStdString();

            LOG_DEBUG(this->debug, "    We got texture info file: " << tFileStr);

            // Test if base name has been seen before, find a unique name if so
            if (this->primTextBaseDict.find(testname) != this->primTextBaseDict.end()) {
                int num = 1;
                std::string candidateName = testname;
                while (this->primTextBaseDict.find(candidateName) != this->primTextBaseDict.end()) {
                    candidateName = testname + std::to_string(num);
                    num++;
                }
                tFileStr = candidateName + suffix;
                this->primTextBaseDict[candidateName] = 1;
            } else {
                this->primTextBaseDict[testname] = 1;
            }

            this->primTextRewriteDict[textureFileKey] = tFileStr;
            this->primTextDict[textureFileKey] = thisTextureId = this->resourceId;
            ++this->resourceId;

            // Add texture to output
            std::string stringy = std::string("<m:texture2d id=\"") + std::to_string(thisTextureId)
                + std::string("\" path=\"/3D/Texture/") + tFileStr + std::string("\" contenttype=\"image/");
            std::string lower_s = return_lower(suffix);
            if (lower_s == std::string(".jpg") || lower_s == std::string(".jpeg")) {
                stringy = stringy + std::string("jpeg\" tilestyleu=\"wrap\" tilestylev=\"wrap\" />\n");
            } else if (lower_s == std::string(".png")) {
                stringy = stringy + std::string("png\" tilestyleu=\"wrap\" tilestylev=\"wrap\" />\n");
            } else {
                std::cerr << "Unaccepted texture file type suffix: " << suffix << std::endl;
                return ErrorCode::BAD_GEO;
            }
            this->modelOutput.append(stringy);
            this->primTexts.push_back(thisTextureId);

        }
    }

    if (!this->hasFileTexture) {
        // No file-based textures, so nothing more to do.
        LOG_DEBUG(this->debug, "Exiting saveTextures");
        return ErrorCode::SUCCESS;
    }

    GA_ROHandleV2 UV_h(gdp, GA_ATTRIB_VERTEX, "uv");
    if (!UV_h.isValid()) {
        std::cerr << "Error: Unable to retreive uv info on a model that uses file-based texture." << std::endl;
        return ErrorCode::BAD_GEO;
    }
    
    // Write out the texture groups.
    // For each entry in our texture dictionary, create a texture2dgroup. For each such group, cycle
    // through all the primitives to see which ones use this texture. If a primitive uses this
    // texture, cycle through its vertices to gather the uv's for this primitive's vertices' use of
    // the texture and create an entry in the text2coord list for each vertex. We begin with
    // vertIndex = 0 and increment it for each vertex so we can do a lookup later.
    for (const auto& pair : this->primTextDict) {
        int textureId = pair.second;
        int vertIndex = 0; // An index into the 3mf list of vertices/points
        std::string stringy = std::string("<m:texture2dgroup id=\"") + std::to_string(this->resourceId)
            + std::string("\" texid=\"") + std::to_string(textureId) + std::string("\">\n");
        this->modelOutput.append(stringy);
        int textGroupId = this->resourceId;
        ++this->resourceId;
        //LOG_DEBUG(this->debug, std::string("Set texture group id to ") + std::to_string(textGroupId));
        //LOG_DEBUG(this->debug, "About to cycle through prims in saveTextures:");
        for (GA_Iterator it(gdp->getPrimitiveRange()); !it.atEnd(); ++it) {
            GA_Offset offset = *it;
            int primNum = static_cast<int>(it.getIndex());
            // I'm still unsure about what I'll see with offsets and indices, so I have this
            // check here to see if I'm wrong.
            
	    /*
            if (static_cast<int>(offset) != primNum) {
                std::cerr << "Prim number " << std::to_string(primNum)
                    << " does not equal offset " << std::to_string(static_cast<int>(offset)) << std::endl;
                return ErrorCode::BAD_GEO;
            }
	    */

            if (this->primTexts[primNum] == textureId) { // This prim uses this texture
                //LOG_DEBUG(this->debug, std::string("Prim ") + std::to_string(primNum) + std::string(" uses texture ")
                    //+ std::to_string(textureId) + std::string(" and we have vertIndex ")
                    //+ std::to_string(vertIndex));
                this->primTextGroup[primNum] = textGroupId;
                // XXX Test if size matches current prim num for sanity check.
                // do vertices
                const GA_OffsetListRef vertices = gdp->getPrimitiveVertexList(it.getOffset());
                UT_Vector2 uv;
                for (GA_Offset vOffset : vertices) {
                    uv = UV_h.get(vOffset);
                    {
                        std::stringstream st;
                        st << std::fixed << std::setprecision(3) << "  <m:tex2coord u=\"" << std::setw(3) << uv[0] << "\" v=\""
                            << std::setw(3) << uv[1] << "\"/>\n";
                        this->modelOutput.append(st.str());
                    }
                }
                // record indices into texture groups for the prim's vertices
                if (this->flip) {
                    std::array<int, 3> newCoords = {vertIndex + 2, vertIndex + 1, vertIndex};
                    this->primTextCoords[primNum] = newCoords;
                } else {
                    std::array<int, 3> newCoords = {vertIndex, vertIndex + 1, vertIndex + 2};
                    this->primTextCoords[primNum] = newCoords;
                }
                
                vertIndex += 3;          
            }

        }
        this->modelOutput.append(std::string("</m:texture2dgroup>\n"));
    }
    
    //LOG_DEBUG(this->debug, "Before leaving saveTextures: \n");
    //printPrimTextCoords(this->primTextCoords, this->debug);

    LOG_DEBUG(this->debug, "Exiting saveTextures");
    return ErrorCode::SUCCESS;
}

//
// Check if a color is in the list of colors for this colorgroup already. If not, add it. Record which primitive and vertex
// should point to the color index in the list of colors for the colorgorup.
//
int
SOP_Save3mf::getOrAddColor(ColorGroup& group, const std::string& color, int primIndex, int localVert, bool& isNew) {
    auto it = group.colorIndex.find(color);
    if(it != group.colorIndex.end()) {
        isNew = false;
        int idx = it->second;
        group.colorIndices[primIndex][localVert] = idx;
        return idx;
    }
    isNew = true;
    int idx = group.colorIndex.size();
    group.colorIndex[color] = idx;
    group.colorIndices[primIndex][localVert] = idx;
    return idx;
}

//
// Return the index in the list of colors for the colorgroup for this primitive and vertex. If there isn't one set,
// return -1.
//
int
SOP_Save3mf::getColorIndex(ColorGroup& group, int primIndex, int localVert) {
    auto it = group.colorIndices.find(primIndex);
    if (it != group.colorIndices.end()) {
        return it->second[localVert];
    }
    return -1; // not found
}


//
// Go through the Houdini data and find all the color attributes -- whether
// on points, vertices, primitives or detail (the whole part). Create a 3mf default color group
// and assign a color to it, so it can be used for the object-level default property.
// Currently I only use a colorgroup for that default property, not a texture, even if the
// rest of the part is texture-only. Also, if the model has detail color, I use that color
// instead of the default (currently white) color for the object-level default property.
// The flow in this routine determines which of Houdini's color types will be written to
// the 3mf file -- vertex, point, primitive, or detail, if a triangle has more than one kind
// of such color attributes. The order of priority is vertex, point, prim, detail. Also, I
// write out a color for each point/vertex/etc., even if the colors are repeated. This should
// clearly be optimized.
//
SOP_Save3mf::ErrorCode
SOP_Save3mf::saveColors(const GU_Detail* gdp) {

    LOG_DEBUG(this->debug, "In saveColors");
    //LOG_DEBUG(this->debug, "Starting saveColors we have output " + this->modelOutput);

    if (this->primColorDict.size() > 0) { // We have triangles that will use a base color texture
        LOG_DEBUG(this->debug, "Geometry has a texture using base color texture.");
        //this->modelOutput.append(std::format("<m:colorgroup id=\"{}\">\n", resourceId));
        {
            std::stringstream st;
            st << "<m:colorgroup id=\"" << this->resourceId << "\">\n";
            this->modelOutput.append(st.str());
        }
        this->primColorTextureGroupId = this->resourceId;
        ++this->resourceId;
        
        // Iterate over prims. If the prim is in the primColorDict (it has base color texture),
        // add that color to the list of colors in the color group.
        const GEO_Primitive *primPtr;
        GA_FOR_ALL_PRIMITIVES(gdp, primPtr) {
            GA_Index pIndex = it.getIndex();
            
            //LOG_DEBUG(this->debug, "primColorDict in use: We got prim id " + std::to_string(pIndex));

            // If prim is in primColorDict then we use the base color texture found there
            if (auto it = this->primColorDict.find(pIndex); it != this->primColorDict.end()) {
                const std::array<float, 3> CdFloat = it->second.color;
                std::string converted = convertColor(CdFloat);
                {
                    std::stringstream st;
                    st << "  <m:color color=\"#";
                    st << converted;
                    st << "\"/>\n";
                    this->modelOutput.append(st.str());
                }
            }            
        }
        this->modelOutput.append("</m:colorgroup>\n");
    }

    // Now go on to do normal color recording -- when we write out the mesh we'll override with
    // primColorTextureGroupId if needed.

    // Check for color according to the order of preference. (Preference order is vertex, point, then prim.
    // We don't attempt to do any blending of color across attribute types. If we
    // do that in the future, we'll need to redo this.

    // Is there a color attribute on vertices? If so, append a colorgroup id. 
    // then loop through prims and get the colors on their vertices. Check if those are already
    // listed, and if so, point to the index of that color in the colorgroup for this prim/vertex.

    GA_Attribute *attrib;
    GA_AttributeOwner myOwner;
    GA_TypeInfo typeinfo;
    bool hasColor = false;
    GA_ROHandleV3 Cd_h;
    GA_AttributeOwner owner_precedence[] = {
        GA_ATTRIB_VERTEX,
        GA_ATTRIB_POINT,
        GA_ATTRIB_PRIMITIVE,
        GA_ATTRIB_DETAIL
    };
    for (GA_AttributeOwner owner : owner_precedence) {
        Cd_h.bind(gdp, owner, "Cd");
        if (Cd_h.isValid()) {
            myOwner = owner;
            LOG_DEBUG(false, "We have color as a " + std::to_string(owner) + " attribute");
            break;
        }
    }
    
    if (!Cd_h.isValid()) {
        LOG_DEBUG(this->debug, "No color on the model.");
        LOG_DEBUG(this->debug, "Exiting saveColors");
        return ErrorCode::SUCCESS;
    }

    this->modelOutput.append("<m:colorgroup id=\"" + std::to_string(this->resourceId) + "\">\n");
    this->colorgroupId = this->resourceId;
    ++this->resourceId;
    // Add this colorgroup if it isn't already there
    this->colorsByGroup.emplace(this->colorgroupId, SOP_Save3mf::ColorGroup());
    LOG_DEBUG(this->debug, "We just added colorgroupId " << this->colorgroupId << " to the data structure");

    UT_Vector3 color;
    switch (myOwner) {
    case GA_ATTRIB_VERTEX:
    {
        LOG_DEBUG(this->debug, "This is a VERTEX attribute");
        this->colorType = ColorTypes::VERTEX;
        GA_Size numPrims = gdp->getNumPrimitives();
        LOG_DEBUG(false, "We have " << numPrims << " primitives.");
        for (GA_Iterator it(gdp->getPrimitiveRange()); !it.atEnd(); ++it) {
            GA_Index pIndex = gdp->primitiveIndex(it.getOffset());
            const GA_Primitive *primPtr = gdp->getPrimitive(it.getOffset());
            GA_Size numVerts = primPtr->getVertexCount();
            //LOG_DEBUG(this->debug, "    There are " + std::to_string(numVerts) + " vertices on this prim.");
            if (numVerts != 3) {
                LOG_DEBUG(this->debug, "Error: Primitives MUST be triangles.");
                std::cerr << "Error: Primitives MUST be triangles." << std::endl;
                return ErrorCode::BAD_GEO;
            }
            //LOG_DEBUG(this->debug, "Prim #" + std::to_string(pIndex));
            const GA_OffsetListRef vertices = gdp->getPrimitiveVertexList(it.getOffset());
            for (int localVert = 0; localVert < 3; localVert++) {
                GA_Offset vOffset = vertices[localVert];
                color = Cd_h.get(vOffset);
                std::array<float, 3> standardColor = {color.x(), color.y(), color.z()};
                std::string converted = convertColor(standardColor);
                bool isNew;
                int colorIndex = getOrAddColor(this->colorsByGroup[this->colorgroupId], converted, pIndex, localVert, isNew);
                
                LOG_DEBUG(this->debug, "prim " << pIndex << " local vert " << localVert << " color " << converted << " index " << colorIndex << " isNew " << isNew);
                if (isNew) {
                    this->modelOutput.append("  <m:color color=\"#");
                    this->modelOutput.append(converted);
                    this->modelOutput.append("\"/>\n");
                    //this->colorsByGroup->colorIndices[primIndex[vOffset] = colorIndex;
                }
            }
        }
        break;
    }
    case GA_ATTRIB_POINT:
    {
        LOG_DEBUG(this->debug, "This is a point attribute");
        this->colorType = ColorTypes::POINT;
        for (GA_Iterator it(gdp->getPrimitiveRange()); !it.atEnd(); ++it) {
            GA_Offset primOffset = *it;
            GA_Index pIndex = gdp->primitiveIndex(primOffset);
            const GA_Primitive *prim = gdp->getPrimitive(primOffset);
            
            for (int localVert = 0; localVert < 3; localVert++) {
                GA_Offset vertOffset = prim->getVertexOffset(localVert);
                GA_Offset pointOffset = gdp->vertexPoint(vertOffset); // get the point for this vertex
                color = Cd_h.get(pointOffset); // look up color by point offset
                
                std::array<float, 3> standardColor = {color.x(), color.y(), color.z()};
                std::string converted = convertColor(standardColor);
                bool isNew;
                int colorIndex = getOrAddColor(this->colorsByGroup[this->colorgroupId], converted, pIndex, localVert, isNew);
                LOG_DEBUG(this->debug, "prim " << pIndex << " local vert " << localVert << " color " << converted << " index " << colorIndex << " isNew " << isNew);
                if (isNew) {
                    this->modelOutput.append("  <m:color color=\"#");
                    this->modelOutput.append(converted);
                    this->modelOutput.append("\"/>\n");
                }
            }
        }
/*
        for (GA_Iterator it(gdp->getPointRange()); !it.atEnd(); ++it) {
            GA_Offset offset = *it;
            color = Cd_h.get(offset);
            std::array<float, 3> standardColor;
            //LOG_DEBUG(this->debug, "Point Offset " + std::to_string(offset)
                //+ " Color R:" + std::to_string(color.x())
                //+ " G:" + std::to_string(color.y())
                //+ " B:" + std::to_string(color.z()));
            standardColor = {color.x(), color.y(), color.z()};
            std::string converted = convertColor(standardColor);

            this->modelOutput.append("  <m:color color=\"#");
            this->modelOutput.append(converted);
            this->modelOutput.append("\"/>\n");
        }
*/
        break;
    }
    case GA_ATTRIB_PRIMITIVE:
    {
       LOG_DEBUG(this->debug, "This is a prim attribute");
        this->colorType = ColorTypes::PRIM;
        for (GA_Iterator it(gdp->getPrimitiveRange()); !it.atEnd(); ++it) {
            GA_Offset offset = *it;
            color = Cd_h.get(offset);
            std::array<float, 3> standardColor;
            //LOG_DEBUG(this->debug, "Prim Offset " + std::to_string(offset)
                //+ " Color R:" + std::to_string(color.x())
                //+ " G:" + std::to_string(color.y())
                //+ " B:" + std::to_string(color.z()));
            standardColor = {color.x(), color.y(), color.z()};
            std::string converted = convertColor(standardColor);
            GA_Index pIndex = gdp->primitiveIndex(offset);
            bool isNew;
            int colorIndex = getOrAddColor(this->colorsByGroup[this->colorgroupId], converted, pIndex, 0, isNew);
            LOG_DEBUG(this->debug, "prim " << pIndex << " local vert " << 0 << " ('cause this is a prim) color " << converted << " index " << colorIndex << " new " << isNew);
            
            if (isNew) {
                this->modelOutput.append("  <m:color color=\"#");
                this->modelOutput.append(converted);
                this->modelOutput.append("\"/>\n");
            }
        }
        break;
    }
    case GA_ATTRIB_DETAIL:
    {
        LOG_DEBUG(this->debug, "This is a detail attribute");
        this->colorType = ColorTypes::DETAIL;
        color = Cd_h.get(0);
        std::array<float, 3> standardColor;
        //LOG_DEB*G(this->debug, "Detail Color R:" + std::to_string(color.x())
                //+ " G:" + std::to_string(color.y())
                //+ " B:" + std::to_string(color.z()));
        standardColor = {color.x(), color.y(), color.z()};
        std::string converted = convertColor(standardColor);

        this->modelOutput.append("  <m:color color=\"#");
        this->modelOutput.append(converted);
        this->modelOutput.append("\"/>\n");
        // Make the default color for the part be the detail color, since that covers the whole part
        this->defaultColorResource = this->colorgroupId;
        break;
    }
    }
    this->modelOutput.append("</m:colorgroup>\n");

    LOG_DEBUG(this->debug, "Exiting saveColors");

    return ErrorCode::SUCCESS;
}


//
// Check if we need to create a default color group. If so, populate it with a
// default color.
//
void
SOP_Save3mf::defaultColor(const GU_Detail* gdp) {
    LOG_DEBUG(this->debug, "Entering defaultColor");

    if (this->colorType == ColorTypes::NONE
        || this->defaultColorResource == -1) {
        LOG_DEBUG(this->debug, "Making a default color group");
        {
            std::stringstream st;
            st << "<m:colorgroup id=\"" << this->resourceId << "\">\n";
            this->modelOutput.append(st.str());
        }
        this->defaultColorResource = this->resourceId;
        // Leave colorgroupId alone since regular color objects will use it and not this last default color group
        ++this->resourceId;
        {
            std::stringstream st;
            st << " <m:color color=\"#";
            st << std::hex << std::uppercase << std::setfill('0');
            st << std::setw(2) << DEFAULT_COLOR[0];
            st << std::setw(2) << DEFAULT_COLOR[1];
            st << std::setw(2) << DEFAULT_COLOR[2];
            st << "\"/>\n";
            st << "</m:colorgroup>\n";
            this->modelOutput.append(st.str());
        }
    }
    return;
}


//
// Add the header to the 3mf model.model file.
//
void
SOP_Save3mf::writeHeader(bool material_ext, bool boolean_ext, bool production_ext) {

    LOG_DEBUG(this->debug, "In writeHeader");
    this->modelOutput.append("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
    this->modelOutput.append("<model unit=\"millimeter\" xml:lang=\"en-US\""); // ToDo: put in mechanism for other units
    if (material_ext) { // Should this really come before the core specification?
        this->modelOutput.append(" xmlns:m=\"http://schemas.microsoft.com/3dmanufacturing/material/2015/02\"");
    }
    this->modelOutput.append(" xmlns=\"http://schemas.microsoft.com/3dmanufacturing/core/2015/02\"");
    if (boolean_ext) {
        this->modelOutput.append(" xmlns:o=\"http://www.hp.com/schemas/3dmanufacturing/booleanoperations/2021/02\"");
    }
    if (production_ext) {
        this->modelOutput.append(" xmlns:p=\"http://schemas.microsoft.com/3dmanufacturing/production/2015/06\"");
    }
    this->modelOutput.append(">\n");
    if (!this->title.empty()) {
        this->modelOutput.append("<metadata name=\"Title\">");
        this->modelOutput.append(this->title);
        this->modelOutput.append("</metadata>\n");
    }
    this->modelOutput.append("<metadata name=\"Application\">");
    this->modelOutput.append("Houdini 3MF Export 3.0</metadata>\n"); // ToDo -- figure out what the version number would actually be
    if (!this->description.empty()) {
        this->modelOutput.append("<metadata name=\"Description\">");
        this->modelOutput.append(this->description);
        this->modelOutput.append("</metadata>\n");
    }
    if (!this->designer.empty()) {
        this->modelOutput.append("<metadata name=\"Designer\">");
        this->modelOutput.append(this->designer);
        this->modelOutput.append("</metadata>\n");
    }
    this->modelOutput.append("<metadata name=\"ModificationDate\">");
    this->modelOutput.append(this->timeString);
    this->modelOutput.append("</metadata>\n");

    LOG_DEBUG(this->debug, "Exiting writeHeader");

    return;
}


//
// Fix the internal opdef filenames into something we can use in an export.
//
bool
extractOpdefTexture(const UT_String &opdef_path, const UT_String &out_path) {

    FS_Reader reader(opdef_path);
    if (!reader.isGood()) {
        std::cerr << "Error: Could not open opdef path: " << opdef_path << std::endl;
        return false;
    }

    std::ofstream out(out_path.c_str(), std::ios::binary);
    if (!out.is_open()) {
        std::cerr << "Error: Could not open output path: " << out_path << std::endl;
        return false;
    }

    UT_IStream *stream = reader.getStream();
    char buf[4096];
    while (!stream->isEof()) {
        exint bytesRead = stream->bread(buf, sizeof(buf));
        if (bytesRead > 0) {
            out.write(buf, bytesRead);
            if (!out.good()) {
                std::cerr << "Error: Failed writing to output file: " << out_path << std::endl;
                return false;
            }
        } else if (bytesRead < 0) {
            std::cerr << "Error: Failed reading from opdef stream: " << opdef_path << std::endl;
            return false;
        }
    }

    out.close();
    if (!out.good()) {
        std::cerr << "Error: Failed closing output file: " << out_path << std::endl;
        return false;
    }

    return true;
}

//
// Create all the files needed for the 3mf archive and then call a function to zip them up.
// For each file we create a temporary one first and then zip them together. This proved
// easier than trying to write the content directly to the archive. The accumulated
// modelOutput string gets pushed here to the 3dmodel.model file. The files we create are
//
SOP_Save3mf::ErrorCode
SOP_Save3mf::doOutput() {

    LOG_DEBUG(this->debug, "In doOutput");
    std::string timestamp = this->timeString;

    std::ostream *sp;
    std::filesystem::path tempDirCrossSystem = std::filesystem::temp_directory_path();
    std::string tmp_path = (tempDirCrossSystem / ("3mf_" + timestamp)).string() + "/";

    // The file that describes all the kinds of contents we can have.
    std::string contenttypesFile = "[Content_Types].xml";
    std::string str_contenttypes =
        "<?xml version=\"1.0\" encoding=\"utf-8\"?> "
        "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\"> "
        "<Default Extension=\"jpeg\" ContentType=\"image/jpeg\"/> "
        "<Default Extension=\"jpg\" ContentType=\"image/jpeg\"/> "
        "<Default Extension=\"model\" ContentType=\"application/vnd.ms-package.3dmanufacturing-3dmodel+xml\"/> "
        "<Default Extension=\"png\" ContentType=\"image/png\"/> "
        "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/> "
        "<Default Extension=\"texture\" ContentType=\"application/vnd.ms-package.3dmanufacturing-3dmodeltexture\"/> "
        "<Default Extension=\"xml\" ContentType=\"application/xml\"/></Types>";
    if (!createIntermediateDirectories(tmp_path + contenttypesFile)) {
        std::cerr << "Error: Could not create path " << tmp_path + contenttypesFile << std::endl;
        return ErrorCode::ZIP_FAILURE;
    }
    FS_Writer contentWriter((tmp_path + contenttypesFile).c_str());
    this->files_to_add.push_back({tmp_path + contenttypesFile, contenttypesFile});
    sp = contentWriter.getStream();
    if (sp == nullptr) {
        std::cerr << "Error: Unable to create writable content types file: " << tmp_path + contenttypesFile << std::endl;
        cleanupFiles(tmp_path, this->debugfiles);
        return ErrorCode::FILE_FAILURE;
    }
    (*sp) << str_contenttypes;
    contentWriter.close();

    // Top-level relationships file
    std::string relsFile = "_rels/.rels";
    std::string str_rels =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?> "
        "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\"> "
        "<Relationship Target=\"/3D/3dmodel.model\" Id=\"rel45876482\" "
        "Type=\"http://schemas.microsoft.com/3dmanufacturing/2013/01/3dmodel\" /> "
        "</Relationships>";
    if (!createIntermediateDirectories(tmp_path + relsFile)) {
        std::cerr << "Error: Could not create path " << tmp_path + relsFile << std::endl;
        cleanupFiles(tmp_path, this->debugfiles);
        return ErrorCode::ZIP_FAILURE;
    }
    FS_Writer relsWriter((tmp_path + relsFile).c_str());
    this->files_to_add.push_back({tmp_path + relsFile, relsFile});
    sp = relsWriter.getStream();
    if (sp == nullptr) {
        std::cerr << "Error: Unable to create writable relations file: " << tmp_path + relsFile << std::endl;
        cleanupFiles(tmp_path, this->debugfiles);
        return ErrorCode::FILE_FAILURE;
    }
    (*sp) << str_rels;
    relsWriter.close();

    // Relationships for the 3dmodel file — includes production extension and textures
    int id = 45876484;
    std::string textRelsFile = "3D/_rels/3dmodel.model.rels";
    std::string str_textRels =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
        "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
        // Declare the production extension as shouldunderstand
        // I believe this means that parsers that don't understand the production format can safely
        // ignore the UUIDs in the file, but those that do understand will have the extension
        // properly declared.              
        "<Relationship Target=\"http://schemas.microsoft.com/3dmanufacturing/production/2015/06\" "
        "Id=\"rel_production\" "
        "Type=\"http://schemas.openxmlformats.org/package/2006/relationships/shouldunderstand\"/>";

    for (const auto& pair : this->primTextDict) {
        std::string texturePath = pair.first;
        std::string rewritten = std::string("3D/Texture/") + this->primTextRewriteDict[texturePath];
        LOG_DEBUG(false, std::string("Looking at texture ") + texturePath);
        LOG_DEBUG(false, std::string("     Will try to move it to ") + tmp_path + rewritten);
        if (!createIntermediateDirectories(tmp_path + rewritten)) {
            std::cerr << "Error: Could not create path " << tmp_path + rewritten << std::endl;
            cleanupFiles(tmp_path, this->debugfiles);
            return ErrorCode::ZIP_FAILURE;
        }
        
        std::string resolvedTexturePath = texturePath;
        LOG_DEBUG(this->debug, "Starting with resolved TexturePath " << resolvedTexturePath);
        if (texturePath.rfind("opdef:", 0) == 0) {
            LOG_DEBUG(this->debug, "Found opdef");
            std::string tempTexPath = tmp_path + "extracted_texture_" 
                + this->primTextRewriteDict[texturePath];
            UT_String utSrc(texturePath.c_str());
            UT_String utDst(tempTexPath.c_str());
            if (!extractOpdefTexture(utSrc, utDst)) {
                std::cerr << "Error: Could not extract opdef texture: " << texturePath << std::endl;
                cleanupFiles(tmp_path, this->debugfiles);
                return ErrorCode::FILE_FAILURE;
            }
            resolvedTexturePath = tempTexPath;
            LOG_DEBUG(this->debug, "Now resolvedTexturePath is " << resolvedTexturePath);
        }
    
        try {
            fs::copy(resolvedTexturePath, tmp_path + rewritten, fs::copy_options::overwrite_existing);
        } catch (const fs::filesystem_error& e) {
            std::cerr << "Error: Filesystem error during copy of texture file: " << e.what() << std::endl;
            cleanupFiles(tmp_path, this->debugfiles);
            return ErrorCode::FILE_FAILURE;
        } catch (const std::exception& e) {
            std::cerr << "Error: Exception during copy of texture file: " << e.what() << std::endl;
            cleanupFiles(tmp_path, this->debugfiles);
            return ErrorCode::FILE_FAILURE;
        }
        this->files_to_add.push_back({tmp_path + rewritten, rewritten});
        // BUG FIX: texture Target should be relative path within archive, not full tmp_path
        str_textRels.append(
            "<Relationship Target=\"/" + rewritten + "\""
            + " Id=\"rel" + std::to_string(id)
            + "\" Type=\"http://schemas.microsoft.com/3dmanufacturing/2013/01/3dtexture\" />");
        ++id;
        LOG_DEBUG(false, std::string("Texture rels file string is now \n") + str_textRels + "\n");
    }

    str_textRels.append("</Relationships>");  // BUG FIX: closing tag was missing!

    if (!createIntermediateDirectories(tmp_path + textRelsFile)) {
        std::cerr << "Error: Could not create path " << tmp_path + textRelsFile << std::endl;
        cleanupFiles(tmp_path, this->debugfiles);
        return ErrorCode::ZIP_FAILURE;
    }
    FS_Writer textRelsWriter((tmp_path + textRelsFile).c_str());
    this->files_to_add.push_back({tmp_path + textRelsFile, textRelsFile});
    sp = textRelsWriter.getStream();
    if (sp == nullptr) {
        std::cerr << "Error: Unable to create writable texture relations file: " << tmp_path + textRelsFile << std::endl;
        cleanupFiles(tmp_path, this->debugfiles);
        return ErrorCode::FILE_FAILURE;
    }
    (*sp) << str_textRels;
    textRelsWriter.close();

    // The file that contains all our model info
    std::string modelFile = "3D/3dmodel.model";
    if (!createIntermediateDirectories(tmp_path + modelFile)) {
        std::cerr << "Error: Could not create path " << tmp_path + modelFile << std::endl;
        cleanupFiles(tmp_path, this->debugfiles);
        return ErrorCode::ZIP_FAILURE;
    }
    FS_Writer modelWriter((tmp_path + modelFile).c_str());
    this->files_to_add.push_back({tmp_path + modelFile, modelFile});
    sp = modelWriter.getStream();
    if (sp == nullptr) {
        std::cerr << "Unable to create writable model file " << tmp_path + modelFile << std::endl;
        cleanupFiles(tmp_path, this->debugfiles);
        return ErrorCode::FILE_FAILURE;
    }
    if (this->modelOutput.length() < 20) {
        std::cerr << "Error: model content is null" << std::endl;
        cleanupFiles(tmp_path, this->debugfiles);
        return ErrorCode::BAD_GEO;
    }
    (*sp) << this->modelOutput;
    modelWriter.close();

    for (const auto& entry : this->files_to_add) {
        LOG_DEBUG(false, "Source path is " + entry.source_path);
        LOG_DEBUG(false, "Archive path is " + entry.archive_path);
    }

    save3mfArchive(this->files_to_add);

    LOG_DEBUG(this->debug, "Exiting doOutput");
    cleanupFiles(tmp_path, this->debugfiles);
    return ErrorCode::SUCCESS;
}


//
// Take all the files in the list of file entries and put them in a zip archive.
//
SOP_Save3mf::ErrorCode
SOP_Save3mf::save3mfArchive(const std::vector<SOP_Save3mf::FileEntry>& files_to_add) {

    // Setup Compression Parameters
    const int method = Z_DEFLATED;
    const int level = Z_DEFAULT_COMPRESSION;
#ifdef _WIN32
    const uint32_t desired_mode = 0100644; // S_IFREG | 0644 — hardcoded since Windows headers lack S_IFREG
#else
    const mode_t desired_mode = S_IFREG | 0644;
#endif

    zip_fileinfo zfi;
    memset(&zfi, 0, sizeof(zfi)); // Initialize structure memory to zero
    // Hardcode the desired mode into the "external_fa" field, shifted by 16 bits
    // This forces the file to be extracted with the requsted permissions on Linux/Fedora.
    zfi.external_fa = (desired_mode << 16) | 0;

    // Open the ZIP archive
    // The last argument means create/overwrite the file
    zipFile zip = zipOpen(this->filename.c_str(), 0); 
    
    if (zip == nullptr) {
        std::cerr << "Error: Could not create ZIP file " << this->filename << std::endl;
        return ErrorCode::ZIP_FAILURE;
    }
    
    long long id = 45876484; 
    
    for (const auto& entry : files_to_add) {
        // Read the file content from disk
        std::string file_data = readFileToString(entry.source_path);

        if (file_data.empty()) {
            std::cerr << "Warning: Skipping empty or unreadable file: " << entry.source_path << std::endl;
            continue;
        }

        // Open a new entry inside the ZIP archive
        int err = zipOpenNewFileInZip(zip,
            entry.archive_path.c_str(), // Name it will have inside the zip
            &zfi, // optional for date/time
            nullptr, 0, nullptr, 0, // Various extra fields
            nullptr, // File comment
            method, // Compression method
            level // Compression level
        );

        if (err != Z_OK) {
            std::cerr << "Error: Could not open archive entry: " << entry.archive_path << std::endl;
            continue; // Should do something better here.
        }

        // Write the file data into the open entry
        err = zipWriteInFileInZip(zip, file_data.data(), file_data.length()); // .length for size in bytes

        if (err != Z_OK) {
            std::cerr << "Error: Failed writing data to archive entry: " << entry.archive_path << std::endl;
        }

        // Close current entry
        zipCloseFileInZip(zip);
    }

    // Close the zip archive
    // Null here write the central directory, finalizing the zip file
    int err = zipClose(zip, nullptr);

    if (err != Z_OK) {
        std::cerr << "Error: Failed to finalize ZIP archive." << std::endl;
        return ErrorCode::ZIP_FAILURE;
    }

    return ErrorCode::SUCCESS;
}
