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
 * We try to adhere to The 3mf Core Specification and most of the 3mf Materials
 * Specification: https://github.com/3mfconsortium
 */


#ifndef __SOP_Save3mf_h__
#define __SOP_Save3mf_h__

#include <string.h>
#include <stdbool.h>
#include <unordered_map>
#include <vector>
#include <SOP/SOP_Node.h>

//
// Utility function for logging debug info
//
inline void
logme(std::string msg, bool writeStatus) {
    // logs to stdout/clog
    if (writeStatus) {
        std::clog << msg << std::endl; // We're debugging, so use endl to flush the buffer.
    }
    return;
}

// Utility for debug statements
#define LOG_DEBUG(condition, message_expression) \
do { \
    if (condition) { \
        std::stringstream ss; \
        ss << "[" << __FUNCTION__ << ":" << __LINE__ << "] " \
           << message_expression; \
        logme(ss.str(), true); \
    } \
} while (0)

namespace HDK_Sample {
/// Save a geometry to the filesystem in 3mf format
class SOP_Save3mf : public SOP_Node
{
public:

    SOP_Save3mf(OP_Network *net, const char *name, OP_Operator *op);
    ~SOP_Save3mf() override;

    // Error codes
    enum class ErrorCode {
        SUCCESS = 0,    // all is okay
        FILE_FAILURE,   // something went wrong with opening the folder, or writing to a file
        ZIP_FAILURE,    // some kind of zip problem
        BAD_GEO         // some kind of problem with the input geometry
    };

    // Types of color we might have
    enum class ColorTypes {
        NONE = 0,
        VERTEX,
        POINT,
        PRIM,
        DETAIL
    };

    // Types of textures we might have
    enum class TextureTypes {
        NONE = 0,
        BASECOLOR,
        FILE
    };

    static PRM_Template myTemplateList[];
    static OP_Node*     myConstructor(OP_Network*, const char *, OP_Operator *);

protected:
    /// Method to cook geometry for the SOP
    OP_ERROR            cookMySop(OP_Context &context) override;

private:
    // For the default transform.
    //static constexpr std::array<float, 12>  TRANSFORM_TRIVIAL = "{1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0}";
    std::string         TRANSFORM_TRIVIAL = "1 0 0 0 1 0 0 0 1 0 0 0";
    // Where to start the counter for resourceId
    static constexpr int    INITIAL_ID = 4;
    // A default name for the mesh for inside the 3mf model.model file header
    static constexpr const char*    MESH_NAME_DEFAULT = "houdini_mesh";
    // Color for default object-level property, unless discovered otherwise
    static constexpr std::array<int, 3>    DEFAULT_COLOR = {0xff, 0xff, 0xff};

    // If parameters change then the SOP recooks, so these don't need to be in callback data
    bool                FLIP(fpreal t) { return evalInt("flip", 0, t); }
    bool                OVERRIDE(fpreal t) { return evalInt("usedShaderOverride", 0, t); }
    void                MESH(std::string& my_mesh, fpreal t)      { UT_StringHolder result; evalString(result, "mesh", 0, t); my_mesh = result.toStdString(); return;}
    void                FILENAME(std::string& my_file, fpreal t)    { UT_StringHolder result; evalString(result, "filename", 0, t); my_file = result.toStdString(); return;}
    void                TITLE(std::string& my_title, fpreal t)   { UT_StringHolder result; evalString(result, "title", 0, t); my_title = result.toStdString(); return;}
    void                DESCRIPTION(std::string& my_description, fpreal t) { UT_StringHolder result; evalString(result, "description", 0, t); my_description = result.toStdString(); return;}
    void                DESIGNER(std::string& my_designer, fpreal t)    { UT_StringHolder result; evalString(result, "designer", 0, t); my_designer = result.toStdString(); return;}
    bool                DEBUG(fpreal t) { return evalInt("debug", 0, t); }
    bool                TIMER(fpreal t) { return evalInt("timer", 0, t); }
    bool                DEBUGFILES(fpreal t) { return evalInt("debugfiles", 0, t); }
    std::string         filename;
    std::string         mesh;
    bool                flip;
    bool                usedShaderOverride;
    bool                debug;
    bool                timer;
    bool                debugfiles;
    std::string         title;
    std::string         description;
    std::string         designer;

    // Set true in save so when cookMySop is forced to run it know it needs to save the geometry
    std::atomic<bool>   saveGeometry;    // Initialized to false in the class constructor's initialization list

    // A struct to hold the mapping from disk file to archive path
    struct FileEntry {
        std::string     source_path;
        std::string     archive_path;
    };

    // A struct to hold base color texture info for a prim
    struct BaseColorInfo {
        int                     index;  // index into color group for this prim
        std::array<float, 3>    color;  // color for this prim

    };
    
    // The colors in a colorgroup
    struct ColorGroup {
        std::unordered_map<std::string, int> colorIndex; // color -> index, for fast lookup
        std::unordered_map<int, std::array<int, 3>> colorIndices; // Record the color index per prim/point/vertex
    };
    
    int getOrAddColor(ColorGroup& group, const std::string& color, int primNum, int vertexSlot, bool& isNew);
    int getColorIndex(ColorGroup& group, int primIndex, int localVert);

    // Timestamp for the beginning  of the save operation.
    std::chrono::time_point<std::chrono::high_resolution_clock>   start;

    // Timestamp string for filename uniqueness, 3mf header info, etc.
    std::string     timeString;

    // Bumped up by one for each new 3mf resource in the file
    int             resourceId = INITIAL_ID; //MUST be > 1 so we can be use a lesser value for our object id

    // ID for the single object we'll be writing out. If we later decide to allow multiple
    // objects for the mesh, we'll have to change this mechanism.
    int             objectId = INITIAL_ID - 1;
    // resourceId for a color group (since resourceId gets incremented for other things)
    int             colorgroupId = -1;

    // A color group for primitives having color-based texture, not file-based
    int             primColorTextureGroupId = -1;

    // Whether the model has file-based texture, at least someplace
    bool            hasFileTexture = false;

    // Type of color on the model
    ColorTypes      colorType = ColorTypes::NONE;

    // What to use for the default color on the model. Not sure I have to do this.
    int             defaultColorResource = -1;

    // In order of prim number, which texture (resourceId) does this prim have? -1 means none.
    std::vector<int>    primTexts;

    // Which texture group does this prim use?
    std::unordered_map<int, int>    primTextGroup;

    // Keys are texture paths, values are the resourceId for the texture
    std::unordered_map<std::string, int>    primTextDict;

    // Keys are the texture base names, values the number of times seen before
    std::unordered_map<std::string, int>    primTextBaseDict;

    // Keys are texture paths, values are rewritten ones that will be unique
    std::unordered_map<std::string, std::string>    primTextRewriteDict;

    // Keys are prim number, values are the colors for base textures that are color-based not file-based
    std::unordered_map<int, BaseColorInfo>  primColorDict;
    
    // Keys are the colorgroup ID, values are the colors in the colorgroup in the hex string format used in the 3mf file
    std::unordered_map<int, ColorGroup> colorsByGroup;

    // Texture coordinate indices into texture groups for each prim with texture
    std::unordered_map<int, std::array<int, 3>>     primTextCoords;

    // A list of the files to put in the archive. We create them in /tmp and then archive them.
    std::vector<FileEntry>  files_to_add;

    // The string that will be written to the model file. It is the text of the 3mf model.
    std::string         modelOutput;

    // Time from the context
    fpreal              t;

    // The locked geometry from our input node -- what we want to save
    const GU_Detail *input_gdb = nullptr;

    static int              save(void *data, int index, fpreal t, const PRM_Template *tplate);
    void                    writeHeader(bool material_ext = false, bool boolean_ext = false, bool production_ext = false);
    SOP_Save3mf::ErrorCode  buildModel(const GU_Detail* gdp);
    SOP_Save3mf::ErrorCode  write3mfItem(const GU_Detail* gdp, int objectId);
    SOP_Save3mf::ErrorCode  write3mfObjectMesh(const GU_Detail* gdp);
    SOP_Save3mf::ErrorCode  saveColors(const GU_Detail* gdp);
    void                    defaultColor(const GU_Detail* gdp);
    SOP_Save3mf::ErrorCode  saveTextures(const GU_Detail* gdp);
    SOP_Save3mf::ErrorCode  doOutput();
    SOP_Save3mf::ErrorCode  save3mfArchive(const std::vector<SOP_Save3mf::FileEntry>& files_to_add);
    SOP_Save3mf::ErrorCode  clearData();
};
} // End HDK_Sample namespace

#endif
