#include "pch.h"
#include <memory>
#include <vector>
#include <chrono>
#include <sstream>
#include <string>
#include <algorithm>
#include <string_view>
#include "PlSqlDevFunctions.hpp"

typedef WCHAR EDITOR_CHAR;
#define EDT_TX(quote) L##quote

LRESULT CALLBACK getMsgProcHook(int nCode, WPARAM wParam, LPARAM lParam);
void selectWord();
void duplicateLine();
void cutSelectionOrLine();
void moveLinesDown();
void moveLinesUp();
char* searchString(char* str, char c);

constexpr auto MENU_ITEM_INDEX_DUPLICATE_LINE = 1;
constexpr auto MENU_ITEM_INDEX_CUT_SELECTION_OR_LINE = 2;
constexpr auto MENU_ITEM_INDEX_MOVE_LINES_DOWN = 3;
constexpr auto MENU_ITEM_INDEX_MOVE_LINES_UP = 4;

typedef std::initializer_list<std::basic_string_view<EDITOR_CHAR>> EditorPatternList;
const EditorPatternList INDENT_AFTER_LINE = { EDT_TX("IF "), EDT_TX("IF("), EDT_TX("FOR "), EDT_TX("FOR("), EDT_TX("LOOP\n"), EDT_TX("DECLARE\n"),
    EDT_TX("BEGIN\n"), EDT_TX("PROCEDURE "), EDT_TX("PROCEDURE\n"), EDT_TX("FUNCTION "), EDT_TX("FUNCTION\n") };
const EditorPatternList INDENT_ADJACENT_LINE = { EDT_TX("ELSEIF "), EDT_TX("ELSEIF("), EDT_TX("ELSE\n"), EDT_TX("IS\n") };
const EditorPatternList INDENT_BEFORE_LINE = { EDT_TX("END;"), EDT_TX("END ") };


HHOOK getMsgProcHookHandle;
int cutMenuItem;
int ideVersion;

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
    return TRUE;
}


const char* IdentifyPlugIn(int nID)
{
    return "Editor enhancements";
}

const char* CreateMenuItem(int nIndex)
{
    switch (nIndex)
    {
    case MENU_ITEM_INDEX_DUPLICATE_LINE:
        return "Edit/Enhancements/Duplicate line";
    case MENU_ITEM_INDEX_CUT_SELECTION_OR_LINE:
        return "Edit/Enhancements/Cut selection or line";
    case MENU_ITEM_INDEX_MOVE_LINES_DOWN:
        return "Edit/Enhancements/Move line down";
    case MENU_ITEM_INDEX_MOVE_LINES_UP:
        return "Edit/Enhancements/Move line up";
    }

    return "";
}

void OnMenuClick(int nIndex)
{
    switch (nIndex)
    {
    case MENU_ITEM_INDEX_DUPLICATE_LINE:
        duplicateLine();
        break;
    case MENU_ITEM_INDEX_CUT_SELECTION_OR_LINE:
        cutSelectionOrLine();
        break;
    case MENU_ITEM_INDEX_MOVE_LINES_DOWN:
        moveLinesDown();
        break;
    case MENU_ITEM_INDEX_MOVE_LINES_UP:
        moveLinesUp();
        break;
    }
}

void OnActivate()
{
    ideVersion = SYS_Version();
    cutMenuItem = IDE_GetMenuItem(ideVersion >= 1200 ?  "edit / clipboard / cut" : "edit / cut"); // Not sure about exact version
    getMsgProcHookHandle = SetWindowsHookEx(WH_GETMESSAGE, getMsgProcHook, (HINSTANCE)NULL, GetCurrentThreadId());
}

void OnDeactivate()
{
    if (getMsgProcHookHandle != NULL)
        UnhookWindowsHookEx(getMsgProcHookHandle);
}

void OnWindowCreated(int windowType)
{
    HWND editorWindow = IDE_GetEditorHandle();

    LRESULT mask = SendMessage(editorWindow, EM_GETEVENTMASK, 0, 0);
    SendMessage(editorWindow, EM_SETEVENTMASK, 0, mask | ENM_SELCHANGE | ENM_LINK | ENM_SCROLL | ENM_CHANGE | ENM_UPDATE);
    SendMessage(editorWindow, EM_AUTOURLDETECT, FALSE, NULL);
}


LRESULT CALLBACK getMsgProcHook(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode < 0)
        return CallNextHookEx(NULL, nCode, wParam, lParam);

    if (nCode == HC_ACTION)
    {
        auto msg = reinterpret_cast<MSG*>(lParam);
        switch (msg->message)
        {
        case WM_LBUTTONUP:
            selectWord();
            break;
        }
    }

    return CallNextHookEx(NULL, nCode, wParam, lParam);
}

bool isWordCharacter(EDITOR_CHAR c)
{
    return std::isalnum(c) || c == EDT_TX('_') || c == EDT_TX('$') || c == EDT_TX('#');
}

void selectWord()
{
    if (!(GetKeyState(VK_CONTROL) & 0x8000))
        return;

    if (!IDE_WindowHasEditor(false))
        return;

    HWND editorWindow = IDE_GetEditorHandle();
    
    int selectionStart, selectionEnd;
    SendMessage(editorWindow, EM_GETSEL, reinterpret_cast<WPARAM>(&selectionStart), reinterpret_cast<WPARAM>(&selectionEnd));

    if (selectionStart != selectionEnd)
        return;

    int currLine = SendMessage(editorWindow, EM_LINEFROMCHAR, selectionStart, NULL);
    int currLineCharIndex = SendMessage(editorWindow, EM_LINEINDEX, currLine, NULL);
    int currLineLength = SendMessage(editorWindow, EM_LINELENGTH, currLineCharIndex, NULL);

    if (currLineLength == 0)
        return;

    auto lineBuffer = std::make_unique<EDITOR_CHAR[]>(currLineLength);
    lineBuffer[0] = currLineLength;
    SendMessage(editorWindow, EM_GETLINE, currLine, reinterpret_cast<WPARAM>(lineBuffer.get()));

    int caretX = selectionStart - currLineCharIndex;
    if (!isWordCharacter(lineBuffer[caretX]))
        return;

    int selFromLineIdx = caretX;
    int selToLineIdx = caretX;
    while (++selToLineIdx < currLineLength)
    {
        if (!isWordCharacter(lineBuffer[selToLineIdx]))
            break;
    }
    while (selFromLineIdx > 0)
    {
        if (!isWordCharacter(lineBuffer[selFromLineIdx - 1]))
            break;
        selFromLineIdx--;
    }

    int selFrom = currLineCharIndex + selFromLineIdx;
    int selTo = currLineCharIndex + selToLineIdx;
    SendMessage(editorWindow, EM_SETSEL, selFrom, selTo);
}

void duplicateLine()
{
    if (!IDE_WindowHasEditor(false) || IDE_GetReadOnly())
        return;

    HWND editorWindow = IDE_GetEditorHandle();
    int cursorX = IDE_GetCursorX() - 1;
    int cursorY = IDE_GetCursorY() - 1;

    int currLineCharIndex = SendMessage(editorWindow, EM_LINEINDEX, cursorY, NULL);
    int currLineLength = SendMessage(editorWindow, EM_LINELENGTH, currLineCharIndex, NULL);

    auto insertionBuffer = std::make_unique<EDITOR_CHAR[]>(currLineLength + 3);
    auto duplicatedLine = insertionBuffer.get() + 2;
    duplicatedLine[0] = currLineLength;
    SendMessage(editorWindow, EM_GETLINE, cursorY, reinterpret_cast<WPARAM>(duplicatedLine));
    insertionBuffer[0] = EDT_TX('\r');
    insertionBuffer[1] = EDT_TX('\n');
    insertionBuffer[currLineLength + 2] = EDT_TX('\0');

    IDE_SetCursor(currLineLength + 1, cursorY + 1);

    SendMessage(editorWindow, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(insertionBuffer.get()));

    IDE_SetCursor(cursorX + 1, cursorY + 2);
}

void cutSelectionOrLine()
{
    if (!IDE_WindowHasEditor(false) || IDE_GetReadOnly())
        return;

    HWND editorWindow = IDE_GetEditorHandle();

    int selectionStart, selectionEnd;
    SendMessage(editorWindow, EM_GETSEL, reinterpret_cast<WPARAM>(&selectionStart), reinterpret_cast<WPARAM>(&selectionEnd));

    if (selectionStart != selectionEnd)
    {
        IDE_SelectMenu(cutMenuItem);
    }
    else
    {
        int cursorX = IDE_GetCursorX() - 1;
        int cursorY = IDE_GetCursorY() - 1;
        int lineCharIndex = SendMessage(editorWindow, EM_LINEINDEX, cursorY, NULL);
        int lineLength = SendMessage(editorWindow, EM_LINELENGTH, lineCharIndex, NULL);
        SendMessage(editorWindow, EM_SETSEL, lineCharIndex, lineCharIndex + lineLength + 2);
        IDE_SelectMenu(cutMenuItem);

        IDE_SetCursor(cursorX + 1, cursorY + 1);
    }
}

bool checkLineIndentPattern(EDITOR_CHAR* textStart, EDITOR_CHAR* lineEnd, const EditorPatternList& patterns)
{
    for (auto pattern : patterns)
    {
        bool matching = true;
        bool restExpectSpace = false;
        for (size_t i = 0; (i < pattern.length()) || (restExpectSpace && textStart + i < lineEnd); i++)
        {
            if (restExpectSpace)
            {
                if (textStart + i == lineEnd)
                    break;

                if (textStart[i] != EDT_TX(' ') && textStart[i] != EDT_TX('\t'))
                {
                    matching = false;
                    break;
                }
            }
            else
            {
                if (pattern[i] == EDT_TX('\n'))
                {
                    restExpectSpace = true;
                }
                else
                {
                    if (textStart + i == lineEnd)
                    {
                        matching = false;
                        break;
                    }

                    if (std::toupper(textStart[i]) != pattern[i])
                    {
                        matching = false;
                        break;
                    }
                }
            }
        }

        if (matching)
            return true;
    }

    return false;
}

EDITOR_CHAR* findFirstNonWhiteChar(EDITOR_CHAR* str, EDITOR_CHAR* end, int& indentValue)
{
    indentValue = 0;
    while (str != end)
    {
        if (*str == EDT_TX(' '))
            indentValue++;
        else if (*str == EDT_TX('\t'))
            indentValue += 3;
        else
            break;

        str++;
    }
    return str;
}

void moveLines(bool moveUp)
{
    if (!IDE_WindowHasEditor(false) || IDE_GetReadOnly())
        return;

    HWND editorWindow = IDE_GetEditorHandle();

    int cursorX = IDE_GetCursorX() - 1;
    int cursorY = IDE_GetCursorY() - 1;

    int selectionStart, selectionEnd;
    SendMessage(editorWindow, EM_GETSEL, reinterpret_cast<WPARAM>(&selectionStart), reinterpret_cast<WPARAM>(&selectionEnd));

    int selectionStartLine = SendMessage(editorWindow, EM_LINEFROMCHAR, selectionStart, NULL);
    int selectionEndLine = SendMessage(editorWindow, EM_LINEFROMCHAR, selectionEnd, NULL);
    
    if (moveUp && selectionStartLine == 0)
        return;

    int lineToMoveCount = selectionEndLine - selectionStartLine + 1;
    int lineToAlterCount = lineToMoveCount + 1;

    int linesToAlterStart, anchorLine, anchorLineIndex;
    if (moveUp)
    {
        linesToAlterStart = selectionStartLine - 1;
        anchorLine = selectionStartLine - 1;
        anchorLineIndex = 0;
    }
    else
    {
        linesToAlterStart = selectionStartLine;
        anchorLine = selectionEndLine + 1;
        anchorLineIndex = lineToMoveCount;
    }

    int totalLines = SendMessage(editorWindow, EM_GETLINECOUNT, NULL, NULL);
    if (linesToAlterStart + lineToAlterCount > totalLines)
        return;
    
    auto lineToAlterRanges = std::make_unique<int[]>(lineToAlterCount + 1);
    lineToAlterRanges[0] = 0;
    for (size_t i = 0; i < lineToAlterCount; i++)
    {
        int line = linesToAlterStart + i;
        int lineCharIndex = SendMessage(editorWindow, EM_LINEINDEX, line, NULL);
        int lineLength = SendMessage(editorWindow, EM_LINELENGTH, lineCharIndex, NULL);
        lineToAlterRanges[i + 1] = lineToAlterRanges[i] + lineLength;
    }

    int linesToAlterBufferSize = lineToAlterRanges[lineToAlterCount];
    auto linesToAlterBuffer = std::make_unique<EDITOR_CHAR[]>(linesToAlterBufferSize);
    for (size_t i = 0; i < lineToAlterCount; i++)
    {
        int line = linesToAlterStart + i;
        static_assert(sizeof(EDITOR_CHAR) == sizeof(WORD));
        int lineLength = lineToAlterRanges[i + 1] - lineToAlterRanges[i];
        if (lineLength > 0)
        {
            auto lineBufferStart = linesToAlterBuffer.get() + lineToAlterRanges[i];
            lineBufferStart[0] = lineLength;
            SendMessage(editorWindow, EM_GETLINE, line, reinterpret_cast<WPARAM>(lineBufferStart));
        }
    }

    auto anchorLineStart = linesToAlterBuffer.get() + lineToAlterRanges[anchorLineIndex];
    std::basic_ostringstream<EDITOR_CHAR> ss;

    if(!moveUp)
        ss.write(anchorLineStart, lineToAlterRanges[anchorLineIndex + 1] - lineToAlterRanges[anchorLineIndex]);

    int anchorLineIndent;
    auto anchorLineTextStart = findFirstNonWhiteChar(linesToAlterBuffer.get() + lineToAlterRanges[anchorLineIndex], linesToAlterBuffer.get() + lineToAlterRanges[anchorLineIndex + 1], anchorLineIndent);
    bool changeIndentation = false;
    bool indentOneMore = false;
    if (anchorLineTextStart != linesToAlterBuffer.get() + lineToAlterRanges[anchorLineIndex + 1])
    {
        auto anchorLineEnd = linesToAlterBuffer.get() + lineToAlterRanges[anchorLineIndex + 1];

        bool isIndentAdjacentToAnchorLine = checkLineIndentPattern(anchorLineTextStart, anchorLineEnd, INDENT_ADJACENT_LINE);
        bool isIndentAfterAnchorLine = !isIndentAdjacentToAnchorLine && checkLineIndentPattern(anchorLineTextStart, anchorLineEnd, INDENT_AFTER_LINE);
        bool isIndentBeforeAnchorLine = !isIndentAfterAnchorLine && checkLineIndentPattern(anchorLineTextStart, anchorLineEnd, INDENT_BEFORE_LINE);

        changeIndentation = isIndentAfterAnchorLine || isIndentAdjacentToAnchorLine || isIndentBeforeAnchorLine;

        if (moveUp)
            indentOneMore = isIndentBeforeAnchorLine || isIndentAdjacentToAnchorLine;
        else
            indentOneMore = isIndentAfterAnchorLine || isIndentAdjacentToAnchorLine;
    }

    int linesToMoveMinIndent = INT_MAX;
    if (changeIndentation)
    {
        for (int lineIdx = moveUp ? 1 : 0; lineIdx < lineToMoveCount + (moveUp ? 1 : 0); lineIdx++)
        {
            auto lineStart = linesToAlterBuffer.get() + lineToAlterRanges[lineIdx];
            auto lineEnd = linesToAlterBuffer.get() + lineToAlterRanges[lineIdx + 1];
            int indent = 0;
            auto textStart = findFirstNonWhiteChar(lineStart, lineEnd, indent);
            linesToMoveMinIndent = std::min(linesToMoveMinIndent, indent);
        }
    }

    for (int lineIdx = moveUp ? 1 : 0; lineIdx < lineToMoveCount + (moveUp ? 1 : 0); lineIdx++)
    {
        if(!moveUp)
            ss << "\r\n";

        auto lineStart = linesToAlterBuffer.get() + lineToAlterRanges[lineIdx];
        auto lineEnd = linesToAlterBuffer.get() + lineToAlterRanges[lineIdx + 1];
        if (changeIndentation)
        {
            ss.write(anchorLineStart, anchorLineTextStart - anchorLineStart);

            int lineIndent;
            auto lineTextStart = findFirstNonWhiteChar(lineStart, lineEnd, lineIndent);
            int additionalIndent = lineIndent - linesToMoveMinIndent;

            if (indentOneMore)
                additionalIndent += 3;

            for (size_t i = 0; i < additionalIndent; i++)
            {
                ss << ' ';
            }

            ss.write(lineTextStart, lineEnd - lineTextStart);
        }
        else
        {
            ss.write(lineStart, lineToAlterRanges[lineIdx + 1] - lineToAlterRanges[lineIdx]);
        }

        if (moveUp)
            ss << "\r\n";
    }

    if (moveUp)
        ss.write(anchorLineStart, lineToAlterRanges[anchorLineIndex + 1] - lineToAlterRanges[anchorLineIndex]);


    int replacementStartLine = linesToAlterStart;
    int replacementEndLine = linesToAlterStart + lineToAlterCount - 1;
    int replacementEndLineLength = lineToAlterRanges[lineToAlterCount] - lineToAlterRanges[lineToAlterCount - 1];
    int replacementStartCharIndex = SendMessage(editorWindow, EM_LINEINDEX, replacementStartLine, NULL);
    int replacementEndCharIndex = SendMessage(editorWindow, EM_LINEINDEX, replacementEndLine, NULL) + replacementEndLineLength;
    SendMessage(editorWindow, EM_SETSEL, replacementStartCharIndex, replacementEndCharIndex);

    SendMessage(editorWindow, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(ss.str().c_str()));

    cursorY += moveUp ? -1 : 1;
    IDE_SetCursor(cursorX + 1, cursorY + 1);
}

void moveLinesDown()
{
    moveLines(false);
}

void moveLinesUp()
{
    moveLines(true);
}

char* searchString(char* str, char c)
{
    while (*str != c && *str != '\0')
        ++str;
    return str;
}