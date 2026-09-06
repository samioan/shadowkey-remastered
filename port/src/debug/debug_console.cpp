#include "debug/debug_console.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <sstream>

#include "debug/debug_metrics.h"

namespace sk_debug {

namespace {

constexpr sk::OverlayColor kPanelColor{8, 10, 18};
constexpr sk::OverlayColor kOutputColor{200, 205, 215};
constexpr sk::OverlayColor kInputColor{235, 235, 235};
constexpr sk::OverlayColor kEchoColor{140, 180, 255};
constexpr sk::OverlayColor kNoticeColor{150, 220, 150};
constexpr sk::OverlayColor kWarningColor{235, 200, 110};
constexpr sk::OverlayColor kErrorColor{240, 120, 120};
constexpr sk::OverlayColor kRuleColor{70, 80, 100};

sk::OverlayColor ColorFor(LineKind kind) {
    switch (kind) {
        case LineKind::Input: return kEchoColor;
        case LineKind::Notice: return kNoticeColor;
        case LineKind::Warning: return kWarningColor;
        case LineKind::Error: return kErrorColor;
        case LineKind::Output:
        default: return kOutputColor;
    }
}

std::string ToLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// The longest prefix every candidate shares -- what Tab fills in when the
// completion is ambiguous.
std::string CommonPrefix(const std::vector<std::string>& candidates) {
    if (candidates.empty()) return std::string();
    std::string prefix = candidates.front();
    for (const std::string& candidate : candidates) {
        size_t i = 0;
        while (i < prefix.size() && i < candidate.size() &&
               std::tolower(static_cast<unsigned char>(prefix[i])) ==
                   std::tolower(static_cast<unsigned char>(candidate[i]))) {
            ++i;
        }
        prefix.resize(i);
    }
    return prefix;
}

}  // namespace

DebugConsole::DebugConsole() {
    Print("Shadowkey debug console -- `help` lists commands, Tab completes, Esc closes.",
          LineKind::Notice);
}

// ---------------------------------------------------------------- registration

void DebugConsole::Register(Command command) {
    const std::string key = ToLower(command.name);
    m_Commands[key] = std::move(command);
}

void DebugConsole::RegisterAlias(const std::string& alias, const std::string& target,
                                  std::vector<std::string> prefixArgs) {
    Command command;
    command.name = alias;
    command.group = "aliases";
    command.usage = alias;
    command.help = "alias for `" + target + "`";
    command.handler = [this, target, prefixArgs](const std::vector<std::string>& args) {
        std::string line = target;
        for (const std::string& arg : prefixArgs) line += " " + arg;
        for (const std::string& arg : args) line += " " + arg;
        return Execute(line);
    };
    Register(std::move(command));
}

// ------------------------------------------------------------------ open/close

void DebugConsole::SetOpen(bool open) {
    if (m_Open == open) return;
    m_Open = open;
    m_Scroll = 0;
    m_HistoryCursor = -1;
    if (!open) {
        m_Input.clear();
        m_Caret = 0;
    }
}

// ----------------------------------------------------------------------- input

bool DebugConsole::HandleKey(ConsoleKey key, bool ctrlDown) {
    if (!m_Open) return false;
    switch (key) {
        case ConsoleKey::Enter:
            if (!m_Input.empty()) {
                Submit(m_Input);
                m_Input.clear();
                m_Caret = 0;
                m_HistoryCursor = -1;
                m_Scroll = 0;
            }
            return true;
        case ConsoleKey::Backspace:
            if (m_Caret > 0) {
                m_Input.erase(m_Caret - 1, 1);
                --m_Caret;
            }
            return true;
        case ConsoleKey::Delete:
            if (m_Caret < m_Input.size()) m_Input.erase(m_Caret, 1);
            return true;
        case ConsoleKey::Tab:
            CompleteCurrentToken();
            return true;
        case ConsoleKey::Left:
            if (m_Caret > 0) --m_Caret;
            return true;
        case ConsoleKey::Right:
            if (m_Caret < m_Input.size()) ++m_Caret;
            return true;
        case ConsoleKey::Home:
            m_Caret = 0;
            return true;
        case ConsoleKey::End:
            m_Caret = m_Input.size();
            return true;
        case ConsoleKey::Up:
            if (ctrlDown) {
                ScrollBy(1);
            } else if (!m_History.empty()) {
                if (m_HistoryCursor < 0) {
                    m_HistoryCursor = static_cast<int>(m_History.size()) - 1;
                } else if (m_HistoryCursor > 0) {
                    --m_HistoryCursor;
                }
                m_Input = m_History[static_cast<size_t>(m_HistoryCursor)];
                m_Caret = m_Input.size();
            }
            return true;
        case ConsoleKey::Down:
            if (ctrlDown) {
                ScrollBy(-1);
            } else if (m_HistoryCursor >= 0) {
                ++m_HistoryCursor;
                if (m_HistoryCursor >= static_cast<int>(m_History.size())) {
                    m_HistoryCursor = -1;
                    m_Input.clear();
                } else {
                    m_Input = m_History[static_cast<size_t>(m_HistoryCursor)];
                }
                m_Caret = m_Input.size();
            }
            return true;
        case ConsoleKey::PageUp:
            ScrollBy(10);
            return true;
        case ConsoleKey::PageDown:
            ScrollBy(-10);
            return true;
        case ConsoleKey::Escape:
            SetOpen(false);
            return true;
        case ConsoleKey::None:
        default:
            // Every other key is swallowed while the console has focus, so a
            // digit typed into the command line never also swings a weapon.
            return true;
    }
}

bool DebugConsole::HandleChar(char ch) {
    if (!m_Open) return false;
    // Control characters (including the 0x0D Enter and 0x08 Backspace that
    // Win32 folds into WM_CHAR) are handled on the key stream instead.
    if (static_cast<unsigned char>(ch) < 0x20 || static_cast<unsigned char>(ch) >= 0x7F) {
        return true;
    }
    m_Input.insert(m_Caret, 1, ch);
    ++m_Caret;
    m_HistoryCursor = -1;
    return true;
}

void DebugConsole::ScrollBy(int lines) {
    m_Scroll = std::clamp(m_Scroll + lines, 0, static_cast<int>(m_Lines.size()));
}

void DebugConsole::CompleteCurrentToken() {
    // Tokenize what has been typed so far. A trailing space means the user
    // has finished a token and is starting the next one, which changes which
    // argument index is being completed.
    const bool startingNewToken = !m_Input.empty() && m_Input.back() == ' ';
    std::vector<std::string> tokens = Tokenize(m_Input);
    if (startingNewToken) tokens.push_back(std::string());
    if (tokens.empty()) tokens.push_back(std::string());

    std::vector<std::string> candidates;
    const std::string partial = ToLower(tokens.back());

    if (tokens.size() == 1) {
        for (const auto& entry : m_Commands) {
            if (entry.first.compare(0, partial.size(), partial) == 0) {
                candidates.push_back(entry.second.name);
            }
        }
    } else {
        auto it = m_Commands.find(ToLower(tokens.front()));
        if (it != m_Commands.end() && it->second.completer) {
            std::vector<std::string> args(tokens.begin() + 1, tokens.end());
            const int argIndex = static_cast<int>(args.size()) - 1;
            for (const std::string& candidate : it->second.completer(argIndex, args)) {
                if (ToLower(candidate).compare(0, partial.size(), partial) == 0) {
                    candidates.push_back(candidate);
                }
            }
        }
    }

    if (candidates.empty()) return;

    std::sort(candidates.begin(), candidates.end());
    candidates.erase(std::unique(candidates.begin(), candidates.end()), candidates.end());

    const std::string completion =
        candidates.size() == 1 ? candidates.front() : CommonPrefix(candidates);
    if (completion.size() >= tokens.back().size()) {
        m_Input.erase(m_Input.size() - tokens.back().size());
        m_Input += completion;
        if (candidates.size() == 1) m_Input += ' ';
        m_Caret = m_Input.size();
    }
    if (candidates.size() > 1) {
        std::string joined;
        for (size_t i = 0; i < candidates.size() && i < 40; ++i) {
            if (i) joined += "  ";
            joined += candidates[i];
        }
        if (candidates.size() > 40) joined += "  ... (" + std::to_string(candidates.size()) + ")";
        Print(joined, LineKind::Output);
    }
}

// ------------------------------------------------------------------- execution

void DebugConsole::Submit(const std::string& line) {
    PushHistory(line);
    m_Queue.push_back(line);
}

void DebugConsole::Drain() {
    // Swapped out first: a command may itself Submit() (an `exec` file, an
    // alias), and those go to the *next* drain rather than extending the one
    // in progress, so a self-submitting command cannot hang the tick.
    std::deque<std::string> batch;
    batch.swap(m_Queue);
    for (const std::string& line : batch) {
        Print("> " + line, LineKind::Input);
        const std::string output = Execute(line);
        if (!output.empty()) PrintMultiline(output);
    }
}

std::string DebugConsole::Execute(const std::string& line) {
    std::vector<std::string> tokens = Tokenize(line);
    if (tokens.empty()) return std::string();

    auto it = m_Commands.find(ToLower(tokens.front()));
    if (it == m_Commands.end()) {
        return "unknown command `" + tokens.front() + "` -- try `help`";
    }
    if (!it->second.handler) return std::string();

    const std::vector<std::string> args(tokens.begin() + 1, tokens.end());
    Count("debug.commands");
    Log("debug", "command: " + line);

    // A console command runs real game and script code. An exception here
    // must not take the process down mid-session -- reporting it into the
    // console is the whole point of having one.
    const bool wasExecuting = m_Executing;
    m_Executing = true;
    std::string result;
    try {
        result = it->second.handler(args);
    } catch (const std::exception& e) {
        result = std::string("exception: ") + e.what();
        Count("debug.command_exceptions");
    } catch (...) {
        result = "unknown exception (a Simkin skException is likely -- see the log)";
        Count("debug.command_exceptions");
    }
    m_Executing = wasExecuting;
    return result;
}

void DebugConsole::PushHistory(const std::string& line) {
    if (line.empty()) return;
    if (!m_History.empty() && m_History.back() == line) return;
    m_History.push_back(line);
    if (m_History.size() > 200) m_History.erase(m_History.begin());
}

// ---------------------------------------------------------------------- output

void DebugConsole::Print(const std::string& text, LineKind kind) {
    m_Lines.push_back({kind, text});
    while (m_Lines.size() > m_MaxLines) m_Lines.pop_front();
    // Everything the console prints also lands in shadowkey_port.log, via
    // the console tee main() installs at startup -- so a session's debug
    // work is reviewable afterwards without screenshots.
    std::printf("[dbg] %s\n", text.c_str());
}

void DebugConsole::PrintMultiline(const std::string& text, LineKind kind) {
    std::istringstream stream(text);
    std::string line;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        Print(line, kind);
    }
}

void DebugConsole::Clear() {
    m_Lines.clear();
    m_Scroll = 0;
}

// -------------------------------------------------------------------- bindings

void DebugConsole::Bind(const std::string& key, const std::string& commandLine) {
    m_Bindings[ToLower(key)] = commandLine;
}

void DebugConsole::Unbind(const std::string& key) {
    m_Bindings.erase(ToLower(key));
}

std::string DebugConsole::BindingFor(const std::string& key) const {
    auto it = m_Bindings.find(ToLower(key));
    return it == m_Bindings.end() ? std::string() : it->second;
}

// ------------------------------------------------------------------- rendering

void DebugConsole::Render(sk::OverlaySurface& surface, float heightFraction) const {
    if (!m_Open) return;
    const int lineHeight = surface.lineHeight();
    const int charWidth = surface.charWidth();
    if (lineHeight <= 0 || charWidth <= 0) return;

    const int panelHeight =
        std::clamp(static_cast<int>(static_cast<float>(surface.height()) * heightFraction),
                   lineHeight * 4, surface.height());
    surface.FillRect(0, 0, surface.width(), panelHeight, kPanelColor, 88);
    surface.FillRect(0, panelHeight - 1, surface.width(), 1, kRuleColor, 100);

    const int padding = 4;
    const int columns = (std::max)(1, (surface.width() - padding * 2) / charWidth);

    // The input line sits on the bottom row of the panel; scrollback fills
    // upward from just above it. Long lines are wrapped into the same column
    // width the caret arithmetic uses.
    const int inputY = panelHeight - lineHeight - 2;
    std::string prompt = "] " + m_Input;
    surface.DrawText(padding, inputY, prompt, kInputColor);
    // Caret: a solid block under the insertion point, drawn rather than
    // blinked -- a blinking caret at a 25Hz present looks broken.
    surface.FillRect(padding + static_cast<int>(2 + m_Caret) * charWidth, inputY + lineHeight - 2,
                     charWidth, 2, kInputColor, 100);

    // Wrap newest-first so the scroll offset counts *display* lines, not
    // logical ones -- otherwise a single long line scrolls unpredictably.
    std::vector<std::pair<LineKind, std::string>> wrapped;
    const int visibleRows = (std::max)(0, (inputY - padding) / lineHeight);
    for (auto it = m_Lines.rbegin();
         it != m_Lines.rend() && static_cast<int>(wrapped.size()) < visibleRows + m_Scroll + 4;
         ++it) {
        std::vector<std::string> pieces;
        const std::string& text = it->text;
        if (text.empty()) {
            pieces.push_back(std::string());
        } else {
            for (size_t start = 0; start < text.size();
                 start += static_cast<size_t>(columns)) {
                pieces.push_back(text.substr(start, static_cast<size_t>(columns)));
            }
        }
        for (auto piece = pieces.rbegin(); piece != pieces.rend(); ++piece) {
            wrapped.emplace_back(it->kind, *piece);
        }
    }

    int y = inputY - lineHeight;
    for (size_t i = static_cast<size_t>(m_Scroll);
         i < wrapped.size() && y >= padding; ++i, y -= lineHeight) {
        surface.DrawText(padding, y, wrapped[i].second, ColorFor(wrapped[i].first));
    }

    if (m_Scroll > 0) {
        const std::string marker = "-- scrolled back " + std::to_string(m_Scroll) + " lines --";
        surface.DrawText(surface.width() - padding - static_cast<int>(marker.size()) * charWidth,
                          padding, marker, kWarningColor);
    }
}

// --------------------------------------------------------------------- helpers

std::vector<std::string> DebugConsole::Tokenize(const std::string& line) {
    std::vector<std::string> tokens;
    std::string current;
    bool inQuotes = false;
    bool haveToken = false;
    for (char ch : line) {
        if (ch == '"') {
            inQuotes = !inQuotes;
            haveToken = true;
            continue;
        }
        if (!inQuotes && (ch == ' ' || ch == '\t')) {
            if (haveToken) {
                tokens.push_back(current);
                current.clear();
                haveToken = false;
            }
            continue;
        }
        current.push_back(ch);
        haveToken = true;
    }
    if (haveToken) tokens.push_back(current);
    return tokens;
}

}  // namespace sk_debug
