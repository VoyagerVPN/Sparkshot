#pragma once
#include <windows.h>
#include <string>
#include <string_view>
#include <stdexcept>
#include <utility>
namespace sparkshot {
enum class Language { English, Russian };
Language ChooseLanguage(std::wstring_view stored, std::wstring_view system);
inline constexpr unsigned FirstTextId = 1000;
enum class TextId : unsigned {
    StartupEnabledForThisCopyOfSparkshot = 1000,
    StartupDisabled = 1001,
    WindowsRejectedThisShortcutItMayBeInUseOrReserve = 1002,
    CaptureShortcutSaved = 1003,
    FolderForScreenshotNumberPng = 1004,
    IsInUseCaptureIsAvailableFromTheSparkshotTrayIco = 1005,
    Copy = 1006,
    Save = 1007,
    Folder = 1008,
    UndoCtrlZ = 1009,
    RedoCtrlY = 1010,
    Color = 1011,
    Hue = 1012,
    Brightness = 1013,
    CustomColors = 1014,
    SelectAnAreaEscToClose = 1015,
    ShiftShapeWheelSizeAltEyedropperCtrlZCtrlYCtrlSh = 1016,
    ScreenCapture = 1017,
    SaveFolder = 1018,
    PressAKeyOrShortcutEGPrintScreenOrCtrlAltSEscCan = 1019,
    Settings = 1020,
    SparkshotCaptureSavingAndStartup = 1021,
    ScreenCapture2 = 1022,
    ShortcutStartsAreaSelection = 1023,
    ShortcutIsInUseChooseAnother = 1024,
    PressANewShortcut = 1025,
    Change = 1026,
    Saving = 1027,
    ScreenshotFolderScreenshotNumberPng = 1028,
    Choose = 1029,
    Startup = 1030,
    OnLaunchThisCopyWhenYouSignInToWindows = 1031,
    OffUseTheSwitchOnTheRight = 1032,
    AnotherCopyTurnOnToReplaceItsPath = 1033,
    ApplicationLocation = 1034,
    YouCanDisableTheOldEntry = 1035,
    StartQuietlyInTheTrayWithoutCapturing = 1036,
    Disable = 1037,
    UnexpectedError = 1038,
    Capture = 1039,
    SaveFolder2 = 1040,
    Exit = 1041,
    ShortcutSelectionCancelled = 1042,
    ToolSelect = 1043,
    ToolPen = 1044,
    ToolLine = 1045,
    ToolArrow = 1046,
    ToolRectangle = 1047,
    ToolEraser = 1048,
    ToolText = 1049,
    ToolPixelate = 1050,
    ToolNumber = 1051,
    ToolEyedropper = 1052,
    ToolEllipse = 1053,
    ToolFill = 1054,
    ErrorCouldNotFindThePathToSparkshotExe = 1055,
    ErrorCouldNotReadWindowsStartupSettings = 1056,
    ErrorThePathIsTooLongForWindowsStartupMoveSparkshotTo = 1057,
    ErrorCouldNotOpenWindowsStartupSettings = 1058,
    ErrorCouldNotChangeWindowsStartupSettings = 1059,
    ErrorTheSettingsSectionIsTooLargeToSaveTheShortcut = 1060,
    ErrorCouldNotSaveTheShortcutNextToTheExe = 1061,
    ErrorCouldNotSaveSettingsNextToTheExe = 1062,
    ErrorCouldNotSaveCustomColorsNextToTheExe = 1063,
    ErrorCouldNotCreateTheColorField = 1064,
    ErrorCouldNotUpdateTheBrightnessBar = 1065,
    ErrorCouldNotUpdateTheBrushMask = 1066,
    ErrorCouldNotCacheTheEditor = 1067,
    ErrorCouldNotRenderTheEditor = 1068,
    ErrorCouldNotRetrieveTheEditorCache = 1069,
    ErrorFramePresentationError = 1070,
    ErrorCouldNotPrepareTheImage = 1071,
    ErrorCouldNotRenderTheScreenshot = 1072,
    ErrorCouldNotStartTextInput = 1073,
    ErrorScreenshotNumberingHasBeenExhausted = 1074,
    ErrorCouldNotCreateTheFileCheckTheFolderAndPermission = 1075,
    ErrorCouldNotWriteThePngTheDiskMayBeFull = 1076,
    ErrorCouldNotInitializeSparkshotGraphics = 1077,
    ErrorTheDxgiFrameReadyEventIsUnavailable = 1078,
    ErrorTheRenderingTestEventIsUnavailable = 1079,
    ErrorWindowsApiErrorWhileProcessingTheImage = 1080,
    ErrorCouldNotCaptureTheDesktop = 1081,
    ErrorUnexpectedPngFormat = 1082,
    ErrorCouldNotReadThePng = 1083,
    ErrorNotEnoughMemoryForTheClipboard = 1084,
    ErrorTheClipboardIsInUseByAnotherApplication = 1085,
    ErrorCouldNotCopyTheImage = 1086
};
inline constexpr size_t TextCount = 87;
std::wstring LoadLocalizedString(HINSTANCE instance, Language language, TextId id);
std::wstring FormatLocalized(std::wstring_view pattern, std::wstring_view value);
class LocalizedError : public std::runtime_error {
public:
    explicit LocalizedError(TextId id, std::wstring detail = {}) : std::runtime_error("Sparkshot error"), id_(id), detail_(std::move(detail)) {}
    TextId Id() const { return id_; }
    const std::wstring& Detail() const { return detail_; }
private:
    TextId id_;
    std::wstring detail_;
};
}
