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


#ifndef __SOP_Read3mf_h__
#define __SOP_Read3mf_h__

#include <string.h>
#include <stdbool.h>
#include <SOP/SOP_Node.h>
#include "tinyxml2.h"
#include <atomic>
#include <UT/UT_Vector.h>
#include <UT/UT_Array.h>
#include <UT/UT_Map.h>
#include <UT/UT_Hash.h>
#include <SYS/SYS_Math.h> // For SYShashCombine if HashUtils isn't found


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

    // Information for each object in objectDict
    struct ObjectData {
        int id;
        UT_String defaultColor;
        int defaultColorGroup = -1;
        //GU_Detail* objGdp = nullptr;
        GU_DetailHandle objGdpHandle;
    };

    // Overload of operator << so I can print maps with ObjectData in them
    inline std::ostream& operator<<(std::ostream& os, const HDK_Sample::ObjectData& data) {
        os << "ID: " << data.id 
           << ", Color: " << data.defaultColor 
           << ", Group: " << data.defaultColorGroup;
        
        // For the GU_DetailHandle, we usually just want to know if it's valid
        if (data.objGdpHandle.isValid()) {
            os << ", [GdpHandle: Valid]";
        } else {
            os << ", [GdpHandle: Null/Invalid]";
        }
        
        return os;
    }

    //
    // Print vectors with << operator
    //
    template <typename T>
    inline std::ostream& operator<<(std::ostream& os, const std::vector<T>& v) {
        os << "[";
        for (size_t i = 0; i < v.size(); ++i) {
            os << v[i] << (i == v.size() - 1 ? "" : ", ");
        }
        os << "]";
        return os;
    }

    // Add this in HDK_Sample namespace so UT_Matrix4 can be printed
    inline std::ostream& operator<<(std::ostream& os, const UT_Matrix4& m) {
        os << "{Matrix4}"; // Or a full loop if you want to see the values
        return os;
    }

    // Forward declaration so class member handleTriangle() can use this in its parameters.
    struct PointKey;

    // Read a geometry from the filesystem in 3mf format
    class SOP_Read3mf : public SOP_Node
    {
    public:

        SOP_Read3mf(OP_Network *net, const char *name, OP_Operator *op);
        ~SOP_Read3mf() override;

        // Error codes
        enum class ErrorCode {
            SUCCESS = 0,    // all is okay
            FILE_FAILURE,   // something went wrong with opening the folder, or writing to a file
            ZIP_FAILURE,    // some kind of zip/unzip problem
            XML_FAILURE,    // some problem parsing the xml file
            BAD_3MF,        // Something was missing or something illegal was found in the 3mf model.
            BAD_GEO,        // some kind of problem with the input geometry
            OTHER           // Another kind of error, such as an inability to lock the geometry.
        };

        // Types of texture tiling
        enum class Tiling {
            WRAP = 0,       // the default
            MIRROR,         // mirror across u or v == 1
            CLAMP,          // streak pixel colors at u or v == 1 through rest of texture
            NONE            // like clamp, but with alpha == 0 (with some exceptons)
        };

        // Types of filters -- we don't handle these yet
        enum class Filters {
            NONE = 0,
            AUTO,
            LINEAR,
            NEAREST
        };

        static PRM_Template myTemplateList[];
        static OP_Node*     myConstructor(OP_Network*, const char *, OP_Operator *);

        static constexpr std::string_view   WHITE = "#ffffff";
        static constexpr std::string_view   DEFAULT_COLOR = WHITE;
        static constexpr int                DEFAULT_GROUP = -1;
        static constexpr std::string_view   EXTRACTFOLDER_DEFAULT = "./read3mf_Assets";
        static constexpr UT_Vector3         DEFAULT_H_COLOR = UT_Vector3(1.0f, 1.0f, 1.0f); // White in Houdini - change if default is not white

    protected:
        /// Method to cook geometry for the SOP
        OP_ERROR            cookMySop(OP_Context &context) override;

    private:
        struct TextureInfo {
            std::array<float, 2> scaling;   // Scaling factors in u and v directions
            Tiling tileU;       // Tiling mode for u
            Tiling tileV;       // Tiling mode for v

            // Default constructor
            TextureInfo()
                : scaling{1.0f, 1.0f},
                tileU(Tiling::WRAP),
                tileV(Tiling::WRAP)
            {
            }
            // Parameterized constructor
            TextureInfo(float u_scale, float v_scale, Tiling u_mode, Tiling v_mode)
                : scaling{u_scale, v_scale},
                tileU(u_mode),
                tileV(v_mode)
            {
            }

            // Overload for TextureInfo
            friend std::ostream& operator<<(std::ostream& os, const TextureInfo& info) {
                os << "{Scale: [" << info.scaling[0] << ", " << info.scaling[1] 
                   << "], TileU: " << (int)info.tileU 
                   << ", TileV: " << (int)info.tileV << "}";
                return os;
            }
        };
        
        struct TextureGroupData {
            UT_String texturePath;
            std::vector<std::array<float, 2>> coords;
            int originalTexId; // Useful for debugging

            // Overload for TextureGroupData
            friend std::ostream& operator<<(std::ostream& os, const TextureGroupData& data) {
                os << "{Path: " << data.texturePath 
                   << ", Coords Count: " << data.coords.size() 
                   << ", OrigID: " << data.originalTexId << "}";
                return os;
            }
        };

        // Dealing with node parameters
        bool                FLIP(fpreal t) { return evalInt("flip", 0, t); }
        bool                BUILD(fpreal t) { return evalInt("build", 0, t); }
        bool                GEO(fpreal t) { return evalInt("geo", 0, t); }
        bool                TIMER(fpreal t) { return evalInt("timer", 0, t); }
        void                FILENAME(std::string& my_file, fpreal t)    { UT_StringHolder result; evalString(result, "filename", 0, t); my_file = result.toStdString(); return;}
        void                ASSETS(std::string& my_assets, fpreal t)   { UT_StringHolder result; evalString(result, "assets", 0, t); my_assets = result.toStdString(); return;}
        bool                DEBUG(fpreal t) { return evalInt("debug", 0, t); }
        std::string         filename;
        bool                flip;
        bool                timer;
        bool                build;
        bool                geoOnly;
        bool                debug;
        std::string         assets;

        // Set true in read so when cookMySop is forced to run it knows it needs to read in the 3mf file
        std::atomic<bool>   loadGeometry;   // Initialized to false in the class constructor's initialization list

        // Node path for utility subnet -- currently unused except for debugging
        UT_String       subnetPath;

        // The path to the node to use for our textures
        UT_String       shaderNode;

        // Timestamp for the beginning  of the read operation.
        std::chrono::time_point<std::chrono::high_resolution_clock>   start;

        // Time from the context
        fpreal          t;

        // Scale factor if the model's units aren't the usual millimeters.
        float           scale_factor = 1.0f;

        // Folder into which we unzip the 3mf contents
        std::string     extractFolder;

        // key: object id, value: the object's default color and a GU_Detail for the object
        UT_Map<int, ObjectData*>                 objectDict;

        // key: colorgroup id, value: array of colors in the group. The colors are represented as strings of 6 characters.
        std::unordered_map<int, std::vector<std::string>>   colorDict;

        // key: basematerials id, value: array of bases (colors) in the group
        std::unordered_map<int, std::vector<std::string>>   basematDict;

        // key: texture2dgroup id (pid on triangles), value: the filename for this group's texture plus the uv coord array
        std::unordered_map<int, TextureGroupData>           texture2dgroupDict;

        // key: texture2dgroup id ('pid' on triangles), value: shader node path
        std::unordered_map<int, std::string>                shaderDict;

        // key: id for multiproperties, value: list of texture2dgroup/colorgroup ids
        std::unordered_map<int, std::vector<int>>           multiPids;

        // key: vertex ("point" in Houdini) number, value: array of (x,y,z) coordinates
        std::map<int, std::vector<float>>                   vertexDict;

        // key: texid, value: TextureInfo (scaling and tiling styles for u and v)
        std::unordered_map<int, TextureInfo>                textureModifyUVsDict;

        // key: texid of texture, value: the file path to the original texture on disk
        std::unordered_map<int, std::string>                textureFilesDict;

        // key: object id if it's listed in the build feature, value: a list of transforms for the object
        UT_Map<int, std::vector<UT_Matrix4>>     buildDict;

        static int                  read(void *data, int index, fpreal t, const PRM_Template *tplate);
        // SOP_Read3mf::ErrorCode   read3mf_archive(const std::vector<SOP_Read3mf::FileEntry>& files_to_add);
        SOP_Read3mf::ErrorCode      getModelFile(std::string rels_path, std::string& model_file);
        SOP_Read3mf::ErrorCode      parseModel(std::string the_model);
        SOP_Read3mf::ErrorCode      handleResources(tinyxml2::XMLElement* element);
        SOP_Read3mf::ErrorCode      handleColorgroup(tinyxml2::XMLElement* element);
        SOP_Read3mf::ErrorCode      handleColor(tinyxml2::XMLElement* element, std::string& color);
        SOP_Read3mf::ErrorCode      handleMultiproperties(tinyxml2::XMLElement* element);
        SOP_Read3mf::ErrorCode      handleBasematerials(tinyxml2::XMLElement* element);
        SOP_Read3mf::ErrorCode      handleBase(tinyxml2::XMLElement* element, std::string& color);
        SOP_Read3mf::ErrorCode      handleTexture2d(tinyxml2::XMLElement* element);
        SOP_Read3mf::ErrorCode      handleTexture2dgroup(tinyxml2::XMLElement* element);
        SOP_Read3mf::ErrorCode      clampTexture(const int texid, const int groupId, std::string texturePathFs,
            bool redoU, bool redoV, float maxU, float maxV, float minU, float minV, std::string& usePath);  
        SOP_Read3mf::ErrorCode      getTexCoord(tinyxml2::XMLElement* element, float& u, float &v);
        SOP_Read3mf::ErrorCode      handleTiling(int id, std::string path, Tiling tilestyleU, Tiling tilestyleV,
            std::string& usePath);
        SOP_Read3mf::ErrorCode      handleBuild(tinyxml2::XMLElement* element);
        SOP_Read3mf::ErrorCode      handleComponents(tinyxml2::XMLElement* element, int parentID);
        SOP_Read3mf::ErrorCode      handleObject(tinyxml2::XMLElement* element);
        SOP_Read3mf::ErrorCode      handleMesh(tinyxml2::XMLElement* element, int& numVertices, int& numTriangles, const int objID);
        SOP_Read3mf::ErrorCode      handleComponent(tinyxml2::XMLElement* element, int parentID);
        SOP_Read3mf::ErrorCode      handleVertices(tinyxml2::XMLElement* element, int& numVertices, const int objID);
        SOP_Read3mf::ErrorCode      handleTriangles(tinyxml2::XMLElement* element, int& numTriangles, const int objID);
        SOP_Read3mf::ErrorCode      handleTriangle(tinyxml2::XMLElement* element, int& numTriangles, const int objID,
            GU_Detail* objGdp, UT_Map<PointKey, GA_Offset>& pointDict, GA_RWHandleID obj_h, GA_RWHandleV3 Cd_h,
            GA_RWHandleV3 UV_h, GA_RWHandleS material_h, GA_RWHandleS override_h);
        SOP_Read3mf::ErrorCode      clearData();
    };
} // End HDK_Sample namespace

#endif
