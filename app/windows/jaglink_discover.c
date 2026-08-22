// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * JAGLINK Discover: deliberately narrow OpenPort 2.0/J2534 evidence tool.
 *
 * The application uses raw J2534 CAN at 500 kbit/s. Passive capture never
 * reaches PassThruWriteMsgs. The bounded inventory path can reach it only
 * through safe_write(), after the portable deny-by-default classifier allows
 * the decoded standard OBD payload.
 */
#include "jaglink/discover_safety.h"
#include "jaglink/evidence.h"

#include <windows.h>
#include <commdlg.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define J2534_STATUS_NOERROR 0L
#define J2534_PROTOCOL_CAN 5UL
#define J2534_PASS_FILTER 1UL
#define J2534_CAN_29BIT_ID 0x00000100UL
#define J2534_CAN_ID_BOTH 0x00000800UL
#define J2534_MAX_DATA 4128U

#define ID_DLL_PATH 1001
#define ID_BROWSE_DLL 1002
#define ID_EVIDENCE_PATH 1003
#define ID_BROWSE_EVIDENCE 1004
#define ID_CONNECT 1005
#define ID_INVENTORY 1006
#define ID_STOP 1007
#define ID_ANNOTATION 1008
#define ID_ADD_ANNOTATION 1009
#define ID_LOG 1010
#define WM_DISCOVER_LOG (WM_APP + 1U)

typedef struct {
    unsigned long ProtocolID;
    unsigned long RxStatus;
    unsigned long TxFlags;
    unsigned long Timestamp;
    unsigned long DataSize;
    unsigned long ExtraDataIndex;
    unsigned char Data[J2534_MAX_DATA];
} J2534Message;

typedef long (WINAPI *J2534OpenFn)(void *, unsigned long *);
typedef long (WINAPI *J2534CloseFn)(unsigned long);
typedef long (WINAPI *J2534ConnectFn)(unsigned long, unsigned long,
                                      unsigned long, unsigned long,
                                      unsigned long *);
typedef long (WINAPI *J2534DisconnectFn)(unsigned long);
typedef long (WINAPI *J2534ReadMsgsFn)(unsigned long, J2534Message *,
                                       unsigned long *, unsigned long);
typedef long (WINAPI *J2534WriteMsgsFn)(unsigned long, J2534Message *,
                                        unsigned long *, unsigned long);
typedef long (WINAPI *J2534StartFilterFn)(unsigned long, unsigned long,
                                          J2534Message *, J2534Message *,
                                          J2534Message *, unsigned long *);
typedef long (WINAPI *J2534StopFilterFn)(unsigned long, unsigned long);

typedef struct {
    J2534OpenFn open;
    J2534CloseFn close;
    J2534ConnectFn connect;
    J2534DisconnectFn disconnect;
    J2534ReadMsgsFn read_msgs;
    J2534WriteMsgsFn write_msgs;
    J2534StartFilterFn start_filter;
    J2534StopFilterFn stop_filter;
} J2534Api;

typedef struct {
    HWND window;
    HWND dll_path;
    HWND evidence_path;
    HWND annotation;
    HWND inventory_button;
    HWND stop_button;
    HWND log;
    HMODULE module;
    J2534Api api;
    unsigned long device_id;
    unsigned long channel_id;
    unsigned long filter_id;
    HANDLE capture_thread;
    volatile LONG capturing;
    bool connected;
    JaglinkEvidenceWriter evidence;
    CRITICAL_SECTION evidence_lock;
    uint64_t captured_frames;
} DiscoverApp;

static DiscoverApp app;

static uint64_t unix_timestamp_us(void)
{
    FILETIME value;
    ULARGE_INTEGER ticks;
    const uint64_t windows_to_unix_100ns = UINT64_C(116444736000000000);

    GetSystemTimeAsFileTime(&value);
    ticks.LowPart = value.dwLowDateTime;
    ticks.HighPart = value.dwHighDateTime;
    if (ticks.QuadPart < windows_to_unix_100ns) return 0U;
    return (ticks.QuadPart - windows_to_unix_100ns) / UINT64_C(10);
}

static void post_log(const char *text)
{
    size_t length;
    char *copy;

    if (text == NULL || app.window == NULL) return;
    length = strlen(text) + 1U;
    copy = (char *)HeapAlloc(GetProcessHeap(), 0U, length);
    if (copy == NULL) return;
    memcpy(copy, text, length);
    if (!PostMessageA(app.window, WM_DISCOVER_LOG, 0U, (LPARAM)copy)) {
        (void)HeapFree(GetProcessHeap(), 0U, copy);
    }
}

static void append_log(HWND control, const char *text)
{
    LRESULT length = SendMessageA(control, WM_GETTEXTLENGTH, 0U, 0U);
    SendMessageA(control, EM_SETSEL, (WPARAM)length, (LPARAM)length);
    SendMessageA(control, EM_REPLACESEL, FALSE, (LPARAM)text);
    SendMessageA(control, EM_REPLACESEL, FALSE, (LPARAM)"\r\n");
}

static bool load_symbol(FARPROC *target, const char *name)
{
    *target = GetProcAddress(app.module, name);
    if (*target == NULL) {
        char message[256];
        (void)snprintf(message, sizeof(message),
                       "J2534 DLL is missing %s.", name);
        post_log(message);
        return false;
    }
    return true;
}

static bool load_j2534(const char *path)
{
    FARPROC symbol;

    app.module = LoadLibraryA(path);
    if (app.module == NULL) {
        post_log("Could not load the selected J2534 DLL. Use the 32-bit "
                 "Tactrix OpenPort 2.0 DLL with the Win32 build.");
        return false;
    }

#define LOAD_J2534(field, type, export_name) \
    do { \
        if (!load_symbol(&symbol, export_name)) return false; \
        app.api.field = (type)symbol; \
    } while (0)

    LOAD_J2534(open, J2534OpenFn, "PassThruOpen");
    LOAD_J2534(close, J2534CloseFn, "PassThruClose");
    LOAD_J2534(connect, J2534ConnectFn, "PassThruConnect");
    LOAD_J2534(disconnect, J2534DisconnectFn, "PassThruDisconnect");
    LOAD_J2534(read_msgs, J2534ReadMsgsFn, "PassThruReadMsgs");
    LOAD_J2534(write_msgs, J2534WriteMsgsFn, "PassThruWriteMsgs");
    LOAD_J2534(start_filter, J2534StartFilterFn,
               "PassThruStartMsgFilter");
    LOAD_J2534(stop_filter, J2534StopFilterFn, "PassThruStopMsgFilter");
#undef LOAD_J2534
    return true;
}

static void unload_j2534(void)
{
    memset(&app.api, 0, sizeof(app.api));
    if (app.module != NULL) {
        (void)FreeLibrary(app.module);
        app.module = NULL;
    }
}

static void write_evidence_frame(JaglinkEvidenceDirection direction,
                                 uint32_t identifier,
                                 bool extended,
                                 const uint8_t *data,
                                 size_t size)
{
    JaglinkEvidenceResult result;

    EnterCriticalSection(&app.evidence_lock);
    result = jaglink_evidence_write_frame(
        &app.evidence, unix_timestamp_us(), direction, identifier, extended,
        data, size, "");
    LeaveCriticalSection(&app.evidence_lock);
    if (result != JAGLINK_EVIDENCE_OK) {
        post_log("Evidence write failed; stop and protect the existing file.");
    }
}

static void log_can_message(const char *direction, uint32_t identifier,
                            const uint8_t *data, size_t size)
{
    char line[256];
    size_t used;
    size_t index;

    used = (size_t)snprintf(line, sizeof(line), "%s %08X  ", direction,
                            (unsigned int)identifier);
    for (index = 0U; index < size && used + 3U < sizeof(line); ++index) {
        int written = snprintf(line + used, sizeof(line) - used, "%02X ",
                               (unsigned int)data[index]);
        if (written < 0) break;
        used += (size_t)written;
    }
    post_log(line);
}

static DWORD WINAPI capture_thread_main(LPVOID context)
{
    J2534Message messages[32];
    (void)context;

    post_log("Passive 500 kbit/s raw CAN capture started.");
    while (InterlockedCompareExchange(&app.capturing, 1L, 1L) != 0L) {
        unsigned long count =
            (unsigned long)(sizeof(messages) / sizeof(messages[0]));
        long status;
        unsigned long index;

        memset(messages, 0, sizeof(messages));
        status = app.api.read_msgs(app.channel_id, messages, &count, 100UL);
        if (status != J2534_STATUS_NOERROR) continue;

        for (index = 0UL; index < count; ++index) {
            J2534Message *message = &messages[index];
            uint32_t identifier;
            bool extended;
            size_t payload_size;

            if (message->DataSize < 4UL ||
                message->DataSize > (unsigned long)J2534_MAX_DATA) continue;
            identifier = ((uint32_t)message->Data[0] << 24U) |
                         ((uint32_t)message->Data[1] << 16U) |
                         ((uint32_t)message->Data[2] << 8U) |
                         (uint32_t)message->Data[3];
            extended = (message->RxStatus & J2534_CAN_29BIT_ID) != 0UL;
            payload_size = (size_t)message->DataSize - 4U;
            write_evidence_frame(JAGLINK_EVIDENCE_RX, identifier, extended,
                                 message->Data + 4U, payload_size);
            app.captured_frames++;
            if (app.captured_frames <= UINT64_C(200) ||
                app.captured_frames % UINT64_C(100) == 0U) {
                log_can_message("RX", identifier, message->Data + 4U,
                                payload_size);
            }
        }
    }
    post_log("Capture stopped.");
    return 0UL;
}

static bool stop_capture(void)
{
    if (app.capture_thread != NULL) {
        DWORD wait_status;
        (void)InterlockedExchange(&app.capturing, 0L);
        wait_status = WaitForSingleObject(app.capture_thread, 5000UL);
        if (wait_status != WAIT_OBJECT_0) {
            post_log("The J2534 read has not returned; the driver remains "
                     "loaded to avoid invalidating an active call. Retry Stop.");
            return false;
        }
        (void)CloseHandle(app.capture_thread);
        app.capture_thread = NULL;
    }
    return true;
}

static bool disconnect_adapter(void)
{
    if (!stop_capture()) return false;
    if (app.connected) {
        if (app.filter_id != 0UL) {
            (void)app.api.stop_filter(app.channel_id, app.filter_id);
            app.filter_id = 0UL;
        }
        (void)app.api.disconnect(app.channel_id);
        (void)app.api.close(app.device_id);
        app.connected = false;
    }
    EnterCriticalSection(&app.evidence_lock);
    jaglink_evidence_close(&app.evidence);
    LeaveCriticalSection(&app.evidence_lock);
    unload_j2534();
    EnableWindow(app.inventory_button, FALSE);
    EnableWindow(app.stop_button, FALSE);
    return true;
}

static bool install_pass_filter(void)
{
    J2534Message mask;
    J2534Message pattern;
    long status;

    memset(&mask, 0, sizeof(mask));
    memset(&pattern, 0, sizeof(pattern));
    mask.ProtocolID = J2534_PROTOCOL_CAN;
    pattern.ProtocolID = J2534_PROTOCOL_CAN;
    mask.DataSize = 4UL;
    pattern.DataSize = 4UL;
    status = app.api.start_filter(app.channel_id, J2534_PASS_FILTER,
                                  &mask, &pattern, NULL, &app.filter_id);
    if (status != J2534_STATUS_NOERROR) {
        post_log("PassThruStartMsgFilter failed; no capture was started.");
        return false;
    }
    return true;
}

static void connect_adapter(void)
{
    char dll_path[MAX_PATH];
    char evidence_path[MAX_PATH];
    long status;

    if (app.connected) {
        post_log("The OpenPort channel is already connected.");
        return;
    }
    (void)GetWindowTextA(app.dll_path, dll_path, (int)sizeof(dll_path));
    (void)GetWindowTextA(app.evidence_path, evidence_path,
                         (int)sizeof(evidence_path));
    if (dll_path[0] == '\0' || evidence_path[0] == '\0') {
        post_log("Select a J2534 DLL and JSONL evidence path first.");
        return;
    }
    if (!load_j2534(dll_path)) {
        unload_j2534();
        return;
    }
    if (jaglink_evidence_open(&app.evidence, evidence_path) !=
        JAGLINK_EVIDENCE_OK) {
        post_log("Could not open the JSON Lines evidence file.");
        unload_j2534();
        return;
    }
    status = app.api.open(NULL, &app.device_id);
    if (status != J2534_STATUS_NOERROR) {
        post_log("PassThruOpen failed. Check the OpenPort driver and cable.");
        (void)disconnect_adapter();
        return;
    }
    status = app.api.connect(app.device_id, J2534_PROTOCOL_CAN,
                             J2534_CAN_ID_BOTH, 500000UL, &app.channel_id);
    if (status != J2534_STATUS_NOERROR) {
        post_log("PassThruConnect failed at raw CAN 500000 bit/s.");
        (void)app.api.close(app.device_id);
        EnterCriticalSection(&app.evidence_lock);
        jaglink_evidence_close(&app.evidence);
        LeaveCriticalSection(&app.evidence_lock);
        unload_j2534();
        return;
    }
    app.connected = true;
    if (!install_pass_filter()) {
        (void)disconnect_adapter();
        return;
    }
    (void)InterlockedExchange(&app.capturing, 1L);
    app.captured_frames = 0U;
    app.capture_thread = CreateThread(NULL, 0U, capture_thread_main, NULL,
                                      0U, NULL);
    if (app.capture_thread == NULL) {
        (void)InterlockedExchange(&app.capturing, 0L);
        post_log("Could not start the capture thread.");
        (void)disconnect_adapter();
        return;
    }
    EnableWindow(app.inventory_button, TRUE);
    EnableWindow(app.stop_button, TRUE);
    post_log("Connected through J2534. Transmit policy remains deny-by-default.");
}

static bool safe_write(const uint8_t *payload, size_t payload_size)
{
    JaglinkDiscoverSafetyDecision safety =
        jaglink_discover_classify_request(payload, payload_size);
    J2534Message message;
    unsigned long count = 1UL;
    long status;
    size_t index;

    if (safety.disposition != JAGLINK_DISCOVER_ALLOW_READ_ONLY_OBD) {
        char line[256];
        (void)snprintf(line, sizeof(line), "BLOCKED service 0x%02X: %s",
                       (unsigned int)safety.service,
                       jaglink_discover_safety_reason_name(safety.reason));
        post_log(line);
        return false;
    }
    if (!app.connected || payload_size == 0U || payload_size > 7U) return false;

    memset(&message, 0, sizeof(message));
    message.ProtocolID = J2534_PROTOCOL_CAN;
    message.DataSize = 12UL;
    message.Data[0] = 0x00U;
    message.Data[1] = 0x00U;
    message.Data[2] = 0x07U;
    message.Data[3] = 0xdfU;
    message.Data[4] = (unsigned char)payload_size;
    memcpy(message.Data + 5U, payload, payload_size);
    for (index = 5U + payload_size; index < 12U; ++index) {
        message.Data[index] = 0x55U;
    }
    status = app.api.write_msgs(app.channel_id, &message, &count, 250UL);
    if (status != J2534_STATUS_NOERROR || count != 1UL) {
        post_log("PassThruWriteMsgs failed for an allowed inventory request.");
        return false;
    }
    write_evidence_frame(JAGLINK_EVIDENCE_TX, UINT32_C(0x7df), false,
                         message.Data + 4U, 8U);
    log_can_message("TX", UINT32_C(0x7df), message.Data + 4U, 8U);
    return true;
}

static void run_inventory(void)
{
    static const struct {
        uint8_t payload[2];
        size_t size;
    } requests[] = {
        {{0x01U, 0x00U}, 2U},
        {{0x01U, 0x20U}, 2U},
        {{0x01U, 0x40U}, 2U},
        {{0x09U, 0x00U}, 2U},
        {{0x09U, 0x02U}, 2U},
        {{0x09U, 0x04U}, 2U},
        {{0x09U, 0x06U}, 2U},
        {{0x09U, 0x08U}, 2U},
        {{0x09U, 0x0aU}, 2U},
        {{0x03U, 0x00U}, 1U},
        {{0x07U, 0x00U}, 1U},
        {{0x0aU, 0x00U}, 1U}
    };
    size_t index;

    if (!app.connected) return;
    EnableWindow(app.inventory_button, FALSE);
    post_log("Starting bounded standard OBD inventory (12 read-only requests).\n");
    for (index = 0U; index < sizeof(requests) / sizeof(requests[0]); ++index) {
        if (!safe_write(requests[index].payload, requests[index].size)) break;
        Sleep(150UL);
    }
    post_log("Bounded inventory request sequence finished; capture continues.");
    EnableWindow(app.inventory_button, TRUE);
}

static void add_annotation(void)
{
    char text[1024];
    JaglinkEvidenceResult result;

    if (!app.connected) {
        post_log("Connect and open an evidence file before annotating.");
        return;
    }
    (void)GetWindowTextA(app.annotation, text, (int)sizeof(text));
    if (text[0] == '\0') return;
    EnterCriticalSection(&app.evidence_lock);
    result = jaglink_evidence_write_annotation(
        &app.evidence, unix_timestamp_us(), text);
    LeaveCriticalSection(&app.evidence_lock);
    if (result == JAGLINK_EVIDENCE_OK) {
        post_log("Operator annotation appended to the evidence stream.");
        SetWindowTextA(app.annotation, "");
    } else {
        post_log("Could not append the operator annotation.");
    }
}

static void choose_dll(HWND owner)
{
    OPENFILENAMEA dialog;
    char path[MAX_PATH];

    (void)GetWindowTextA(app.dll_path, path, (int)sizeof(path));
    memset(&dialog, 0, sizeof(dialog));
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = owner;
    dialog.lpstrFilter = "J2534 DLL (*.dll)\0*.dll\0All files\0*.*\0";
    dialog.lpstrFile = path;
    dialog.nMaxFile = (DWORD)sizeof(path);
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (GetOpenFileNameA(&dialog)) SetWindowTextA(app.dll_path, path);
}

static void choose_evidence(HWND owner)
{
    OPENFILENAMEA dialog;
    char path[MAX_PATH];

    (void)GetWindowTextA(app.evidence_path, path, (int)sizeof(path));
    memset(&dialog, 0, sizeof(dialog));
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = owner;
    dialog.lpstrFilter = "JSON Lines (*.jsonl)\0*.jsonl\0All files\0*.*\0";
    dialog.lpstrFile = path;
    dialog.nMaxFile = (DWORD)sizeof(path);
    dialog.lpstrDefExt = "jsonl";
    dialog.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT;
    if (GetSaveFileNameA(&dialog)) SetWindowTextA(app.evidence_path, path);
}

static HWND control(HWND parent, const char *class_name, const char *text,
                    DWORD style, int x, int y, int width, int height,
                    int identifier)
{
    return CreateWindowExA(0U, class_name, text, WS_CHILD | WS_VISIBLE | style,
                           x, y, width, height, parent,
                           (HMENU)(INT_PTR)identifier,
                           GetModuleHandleA(NULL), NULL);
}

static void create_controls(HWND window)
{
    HFONT font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    HWND item;

#define ADD_CONTROL(target, class_name, text, style, x, y, w, h, identifier) \
    do { \
        target = control(window, class_name, text, style, x, y, w, h, \
                         identifier); \
        (void)SendMessageA(target, WM_SETFONT, (WPARAM)font, TRUE); \
    } while (0)

    ADD_CONTROL(item, "STATIC",
                "JAGLINK Discover — passive evidence capture and bounded "
                "read-only inventory",
                SS_LEFT, 18, 14, 860, 22, 0);
    ADD_CONTROL(item, "STATIC",
                "Safety boundary: writes, resets, security access, routines, "
                "DTC clearing and programming are blocked.",
                SS_LEFT, 18, 40, 860, 22, 0);
    ADD_CONTROL(item, "STATIC", "OpenPort/J2534 DLL", SS_LEFT,
                18, 76, 150, 20, 0);
    ADD_CONTROL(app.dll_path, "EDIT", "",
                WS_BORDER | ES_AUTOHSCROLL, 170, 72, 580, 25, ID_DLL_PATH);
    ADD_CONTROL(item, "BUTTON", "Browse...", BS_PUSHBUTTON,
                762, 71, 110, 27, ID_BROWSE_DLL);
    ADD_CONTROL(item, "STATIC", "Evidence JSONL", SS_LEFT,
                18, 111, 150, 20, 0);
    ADD_CONTROL(app.evidence_path, "EDIT", "jaglink-discover-evidence.jsonl",
                WS_BORDER | ES_AUTOHSCROLL, 170, 107, 580, 25,
                ID_EVIDENCE_PATH);
    ADD_CONTROL(item, "BUTTON", "Browse...", BS_PUSHBUTTON,
                762, 106, 110, 27, ID_BROWSE_EVIDENCE);
    ADD_CONTROL(item, "BUTTON", "Start passive capture", BS_PUSHBUTTON,
                18, 148, 190, 32, ID_CONNECT);
    ADD_CONTROL(app.inventory_button, "BUTTON", "Run read-only OBD inventory",
                BS_PUSHBUTTON, 220, 148, 230, 32, ID_INVENTORY);
    ADD_CONTROL(app.stop_button, "BUTTON", "Stop / disconnect", BS_PUSHBUTTON,
                462, 148, 170, 32, ID_STOP);
    EnableWindow(app.inventory_button, FALSE);
    EnableWindow(app.stop_button, FALSE);
    ADD_CONTROL(item, "STATIC", "Operator annotation", SS_LEFT,
                18, 198, 150, 20, 0);
    ADD_CONTROL(app.annotation, "EDIT", "", WS_BORDER | ES_AUTOHSCROLL,
                170, 194, 580, 25, ID_ANNOTATION);
    ADD_CONTROL(item, "BUTTON", "Append", BS_PUSHBUTTON,
                762, 193, 110, 27, ID_ADD_ANNOTATION);
    ADD_CONTROL(app.log, "EDIT", "Ready. No vehicle traffic has been sent.\r\n",
                WS_BORDER | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL |
                    ES_READONLY,
                18, 236, 854, 360, ID_LOG);
#undef ADD_CONTROL
}

static LRESULT CALLBACK window_proc(HWND window, UINT message,
                                    WPARAM wparam, LPARAM lparam)
{
    switch (message) {
    case WM_CREATE:
        app.window = window;
        create_controls(window);
        return 0L;
    case WM_COMMAND:
        if (HIWORD(wparam) == BN_CLICKED) {
            switch (LOWORD(wparam)) {
            case ID_BROWSE_DLL: choose_dll(window); return 0L;
            case ID_BROWSE_EVIDENCE: choose_evidence(window); return 0L;
            case ID_CONNECT: connect_adapter(); return 0L;
            case ID_INVENTORY: run_inventory(); return 0L;
            case ID_STOP:
                if (disconnect_adapter()) post_log("Disconnected.");
                return 0L;
            case ID_ADD_ANNOTATION: add_annotation(); return 0L;
            default: break;
            }
        }
        break;
    case WM_DISCOVER_LOG: {
        char *text = (char *)lparam;
        if (text != NULL) {
            append_log(app.log, text);
            (void)HeapFree(GetProcessHeap(), 0U, text);
        }
        return 0L;
    }
    case WM_CLOSE:
        if (disconnect_adapter()) DestroyWindow(window);
        return 0L;
    case WM_DESTROY:
        app.window = NULL;
        PostQuitMessage(0);
        return 0L;
    default:
        break;
    }
    return DefWindowProcA(window, message, wparam, lparam);
}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous_instance,
                   LPSTR command_line, int show_command)
{
    const char class_name[] = "JAGLINKDiscoverWindow";
    WNDCLASSEXA window_class;
    HWND window;
    MSG message;
    int result;

    (void)previous_instance;
    (void)command_line;
    memset(&app, 0, sizeof(app));
    app.evidence = (JaglinkEvidenceWriter)JAGLINK_EVIDENCE_WRITER_INIT;
    InitializeCriticalSection(&app.evidence_lock);

    memset(&window_class, 0, sizeof(window_class));
    window_class.cbSize = sizeof(window_class);
    window_class.lpfnWndProc = window_proc;
    window_class.hInstance = instance;
    window_class.hCursor = LoadCursorA(NULL, IDC_ARROW);
    window_class.hIcon = LoadIconA(NULL, IDI_APPLICATION);
    window_class.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    window_class.lpszClassName = class_name;
    if (RegisterClassExA(&window_class) == 0U) {
        DeleteCriticalSection(&app.evidence_lock);
        return 1;
    }

    window = CreateWindowExA(
        0U, class_name, "JAGLINK Discover — OpenPort 2.0 / J2534",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 910, 650, NULL, NULL, instance, NULL);
    if (window == NULL) {
        DeleteCriticalSection(&app.evidence_lock);
        return 1;
    }
    ShowWindow(window, show_command);
    UpdateWindow(window);

    while ((result = (int)GetMessageA(&message, NULL, 0U, 0U)) > 0) {
        TranslateMessage(&message);
        DispatchMessageA(&message);
    }
    (void)disconnect_adapter();
    DeleteCriticalSection(&app.evidence_lock);
    return result < 0 ? 1 : (int)message.wParam;
}
