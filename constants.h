#ifndef CONSTANTS_H
#define CONSTANTS_H

#include <QString>

// ---------------------------------------------------------------------------
// Log output HTML colors
// ---------------------------------------------------------------------------
namespace LogColor {
    constexpr const char *Info     = "#3498db";
    constexpr const char *Success  = "#27ae60";
    constexpr const char *Error    = "#e74c3c";
    constexpr const char *Warning  = "#e67e22";
    constexpr const char *Command  = "#95a5a6";

    constexpr const char *FixApplied  = "#2ecc71";
    constexpr const char *SevError    = "#e74c3c";
    constexpr const char *SevWarning  = "#f39c12";
    constexpr const char *SevInfo     = "#7f8c8d";

    constexpr const char *InvalidPath = "#FF0000";
}

// ---------------------------------------------------------------------------
// Report size limits (must match Worker and CLI constraints)
// ---------------------------------------------------------------------------
namespace ReportLimits {
    constexpr qint64 MaxFileSize  = 10 * 1024 * 1024; // 10 MB per file
    constexpr qint64 MaxTotalSize = 25 * 1024 * 1024;  // 25 MB total
}

// ---------------------------------------------------------------------------
// OpenGL surface defaults
// ---------------------------------------------------------------------------
namespace GLDefaults {
    constexpr int MajorVersion   = 3;
    constexpr int MinorVersion   = 3;
    constexpr int DepthBits      = 24;
    constexpr int Samples        = 4;
}

// ---------------------------------------------------------------------------
// CLI subprocess
// ---------------------------------------------------------------------------
namespace CliDefaults {
    constexpr int ProcessTimeoutMs = 10000;
#ifdef Q_OS_WIN
    constexpr const char *BinaryName = "cleanmodels.exe";
#else
    constexpr const char *BinaryName = "cleanmodels";
#endif
}

// ---------------------------------------------------------------------------
// CLI flags — single source of truth for flag strings passed to cleanmodels
// ---------------------------------------------------------------------------
namespace CliCommand {
    constexpr const char *Repair    = "repair";
    constexpr const char *Decompile = "decompile";
    constexpr const char *Compile   = "compile";
}

namespace CliFlag {
    constexpr const char *JsonLines       = "--json-lines";
    constexpr const char *Check           = "--check";
    constexpr const char *StripDegenerate = "--strip-degenerate";
    constexpr const char *FixAnimations   = "--fix-animations";
    constexpr const char *FixPivots       = "--fix-pivots";
    constexpr const char *FixTilefade     = "--fix-tilefade";
    constexpr const char *TilefadeZ       = "--tilefade-z";
    constexpr const char *FixAabb         = "--fix-aabb";
    constexpr const char *ReparentChildren = "--reparent-children";
    constexpr const char *WrapRoot        = "--wrap-root";
    constexpr const char *SplitMultiedge  = "--split-multiedge";
    constexpr const char *Scale           = "--scale";
    constexpr const char *ScaleX          = "--scale-x";
    constexpr const char *ScaleY          = "--scale-y";
    constexpr const char *ScaleZ          = "--scale-z";
    constexpr const char *Classification  = "--classification";
    constexpr const char *Snap            = "--snap";
    constexpr const char *TvertSnap       = "--tvert-snap";
    constexpr const char *Render          = "--render";
    constexpr const char *Shadow          = "--shadow";
    constexpr const char *ForceWhite      = "--force-white";
    constexpr const char *MergeByBitmap   = "--merge-by-bitmap";
    constexpr const char *CullInvisible   = "--cull-invisible";
    constexpr const char *PlaceableTrans  = "--placeable-transparency";
    constexpr const char *TransparencyKey = "--transparency-key";
    constexpr const char *PivotAllowSplit = "--pivot-allow-split";
    constexpr const char *PivotBelowZ0    = "--pivot-below-z0";
    constexpr const char *PivotMoveBad    = "--pivot-move-bad";
    constexpr const char *PivotSmoothing  = "--pivot-smoothing";
    constexpr const char *PivotMinFaces   = "--pivot-min-faces";
    constexpr const char *PivotSplitFirst = "--pivot-split-first";
    constexpr const char *Water           = "--water";
    constexpr const char *WaterKey        = "--water-key";
    constexpr const char *DynamicWater    = "--dynamic-water";
    constexpr const char *WaveHeight      = "--wave-height";
    constexpr const char *RotateWater     = "--rotate-water";
    constexpr const char *RetileWater     = "--retile-water";
    constexpr const char *Foliage         = "--foliage";
    constexpr const char *FoliageKey      = "--foliage-key";
    constexpr const char *Splotch         = "--splotch";
    constexpr const char *SplotchKey      = "--splotch-key";
    constexpr const char *RotateGround    = "--rotate-ground";
    constexpr const char *GroundKey       = "--ground-key";
    constexpr const char *Chamfer         = "--chamfer";
    constexpr const char *RetileGround    = "--retile-ground";
    constexpr const char *RaiseLower      = "--raise-lower";
    constexpr const char *RaiseAmount     = "--raise-amount";
    constexpr const char *RemapWalkmesh   = "--remap-walkmesh-material";
    constexpr const char *TilefadeUndo    = "--tilefade-undo";
}

// ---------------------------------------------------------------------------
// QSettings keys — single source of truth for persistent option keys
// ---------------------------------------------------------------------------
namespace Setting {
    constexpr const char *Geometry       = "geometry";
    constexpr const char *OptionsGroup   = "options";

    constexpr const char *InDir          = "indir";
    constexpr const char *OutDir         = "outdir";
    constexpr const char *Pattern        = "pattern";
    constexpr const char *Classification = "classification";

    constexpr const char *AllFixes       = "all_fixes";
    constexpr const char *FixValidate    = "fix_validate";
    constexpr const char *FixStripDegen  = "fix_strip_degen";
    constexpr const char *FixAnimations  = "fix_animations";
    constexpr const char *FixPivots      = "fix_pivots";
    constexpr const char *FixTilefade    = "fix_tilefade";
    constexpr const char *FixAabb        = "fix_aabb";
    constexpr const char *FixReparent    = "fix_reparent";
    constexpr const char *FixWrapRoot    = "fix_wrap_root";
    constexpr const char *FixSplitMultiedge = "fix_split_multiedge";

    constexpr const char *RescaleX       = "rescale_x";
    constexpr const char *RescaleY       = "rescale_y";
    constexpr const char *RescaleZ       = "rescale_z";
    constexpr const char *Snap           = "snap";
    constexpr const char *TvertSnap      = "tvert_snap";
    constexpr const char *RenderMode     = "render";
    constexpr const char *ShadowMode     = "shadow";
    constexpr const char *ForceWhite     = "force_white";
    constexpr const char *MergeByBitmap  = "merge_by_bitmap";
    constexpr const char *InvisibleMeshCull = "invisible_mesh_cull";
    constexpr const char *PlaceableTrans = "placeable_with_transparency";
    constexpr const char *TransparencyKey = "transparency_key";

    constexpr const char *TilefadeUndo   = "tilefade_undo";
    constexpr const char *SliceHeight    = "slice_height";
    constexpr const char *DoWater        = "do_water";
    constexpr const char *DynamicWater   = "dynamic_water";
    constexpr const char *WaveHeight     = "wave_height";
    constexpr const char *WaterKey       = "water_key";
    constexpr const char *RotateWater    = "rotate_water";
    constexpr const char *TileWater      = "tile_water";
    constexpr const char *Foliage        = "foliage";
    constexpr const char *FoliageKey     = "foliage_key";
    constexpr const char *AnimateSplotches = "animate_splotches";
    constexpr const char *SplotchKey     = "splotch_key";
    constexpr const char *RotateGround   = "rotate_ground";
    constexpr const char *ChamferMode    = "chamfer";
    constexpr const char *TileGround     = "tile_ground";
    constexpr const char *GroundKey      = "ground_key";
    constexpr const char *TileRaise      = "tile_raise";
    constexpr const char *TileRaiseAmount = "tile_raise_amount";
    constexpr const char *MapAabbMaterial = "map_aabb_material";
    constexpr const char *MapAabbFrom    = "map_aabb_from";
    constexpr const char *MapAabbTo      = "map_aabb_to";

    constexpr const char *AllowSplit     = "allow_split";
    constexpr const char *PivotsBelowZ0  = "pivots_below_z0";
    constexpr const char *MoveBadPivots  = "move_bad_pivots";
    constexpr const char *SmoothingGroups = "smoothing_groups";
    constexpr const char *MinSize        = "min_size";
    constexpr const char *SplitFirst     = "split_first";

    constexpr const char *CameraRotSens  = "camera_rotation_sensitivity";
    constexpr const char *CameraPanScale = "camera_pan_scale";
    constexpr const char *CameraZoomFactor = "camera_zoom_factor";

    constexpr const char *SidebarVisible = "sidebar_visible";
    constexpr const char *RawLogVisible  = "raw_log_visible";
}

// ---------------------------------------------------------------------------
// Combo option sets — {display label, CLI value} pairs
// First item's CLI value is empty string = "no selection / default"
// ---------------------------------------------------------------------------
struct ComboOption {
    const char *label;
    const char *cliValue;
};

namespace Options {
    constexpr ComboOption Classification[] = {
        {"Automatic", ""}, {"Character", "CHARACTER"}, {"Door", "DOOR"},
        {"Effect", "EFFECT"}, {"Item", "ITEM"}, {"Tile", "TILE"}
    };
    constexpr ComboOption Snap[] = {
        {"None", ""}, {"Binary (1/128)", "binary"},
        {"Decimal (0.01)", "decimal"}, {"Fine (0.001)", "fine"}
    };
    constexpr ComboOption TvertSnap[] = {
        {"None", ""}, {"256", "256"}, {"512", "512"}, {"1024", "1024"}
    };
    constexpr ComboOption RenderOverride[] = {
        {"Default", ""}, {"All", "all"}, {"None", "none"}
    };
    constexpr ComboOption ShadowOverride[] = {
        {"Default", ""}, {"All", "all"}, {"None", "none"}
    };
    constexpr ComboOption PivotBelowZ0[] = {
        {"Disallow", "disallow"}, {"Allow", "allow"}, {"Slice", "slice"}
    };
    constexpr ComboOption PivotMoveBad[] = {
        {"No", "no"}, {"Top", "top"}, {"Middle", "middle"}, {"Bottom", "bottom"}
    };
    constexpr ComboOption PivotSmoothing[] = {
        {"Use", "use"}, {"Protect", "protect"}, {"Ignore", "ignore"}
    };
    constexpr ComboOption PivotSplitFirst[] = {
        {"Convex", "convex"}, {"Concave", "concave"}
    };
    constexpr ComboOption DynamicWater[] = {
        {"Yes", "yes"}, {"No", "no"}, {"Wavy", "wavy"}
    };
    constexpr ComboOption RotateToggle[] = {
        {"No change", ""}, {"1", "1"}, {"0", "0"}
    };
    constexpr ComboOption RetileSize[] = {
        {"No change", ""}, {"1x1", "1"}, {"2x2", "2"}, {"3x3", "3"}
    };
    constexpr ComboOption Foliage[] = {
        {"No change", ""}, {"Tilefade", "tilefade"}, {"Animate", "animate"},
        {"De-animate", "de-animate"}, {"Ignore", "ignore"}
    };
    constexpr ComboOption Chamfer[] = {
        {"No change", ""}, {"Add", "add"}, {"Delete", "delete"}
    };
    constexpr ComboOption RaiseLower[] = {
        {"None", ""}, {"Raise", "raise"}, {"Lower", "lower"}
    };
}

// ---------------------------------------------------------------------------
// Layout tokens
// ---------------------------------------------------------------------------
namespace Layout {
    constexpr int RootMargin       = 8;
    constexpr int DefaultSpacing   = 6;
    constexpr int CompactSpacing   = 4;
    constexpr int SectionGap       = 14;
    constexpr int IndentLeft       = 16;

    constexpr int GroupMarginH     = 8;
    constexpr int GroupMarginTop   = 6;
    constexpr int GroupMarginBottom = 6;

    constexpr int SidebarTopPad       = 8;
    constexpr int SidebarMinWidth     = 320;
    constexpr int SidebarMaxWidth     = 480;
    constexpr int SidebarDefaultWidth = 380;
    constexpr int CleanButtonHeight   = 36;
    constexpr int TableRowHeight      = 24;
}

// ---------------------------------------------------------------------------
// Viewport colors
// ---------------------------------------------------------------------------
namespace ViewportColor {
    constexpr float BgR = 0.18f;
    constexpr float BgG = 0.20f;
    constexpr float BgB = 0.25f;

    constexpr float GridR = 0.35f;
    constexpr float GridG = 0.35f;
    constexpr float GridB = 0.40f;

    constexpr float RefR = 0.4f;
    constexpr float RefG = 0.6f;
    constexpr float RefB = 0.9f;
}

#endif // CONSTANTS_H
