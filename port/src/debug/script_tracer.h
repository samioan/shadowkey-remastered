#pragma once

// M68: script-side instrumentation.
//
// The vendored Simkin interpreter carries two hooks that this port has
// never used, and between them they answer "what is actually running":
//
//   * `skInterpreter::setTraceCallback(skTraceCallback*)` receives the
//     interpreter's own trace output. With `Interpreter.Tracing` set, that
//     includes every method call as scripts execute -- the hit side of the
//     native bridge, which `SoftFailNativeCall`'s observer cannot see
//     because a handled call never reaches it (each binding class owns its
//     own `method()` override; there is no shared dispatch point).
//
//   * `skInterpreter::setStatementStepper(skStatementStepper*)` is called
//     before every statement, and separately on every script exception and
//     `breakpoint` command. That gives a per-frame statement count -- the
//     cheapest honest measure of how much script work a frame did -- and
//     turns a script exception into a debug event rather than a line that
//     scrolls past in the log.
//
// **Two behavioural landmines, both handled here and worth stating plainly
// because getting either wrong would change how the game runs:**
//
//   1. `statementExecuted`/`compoundStatementExecuted` returning false
//      *halts the current method*. This tracer always returns true.
//   2. `exceptionEncountered` returning false *swallows the exception* and
//      continues. This tracer always returns true, so every exception is
//      thrown exactly as it would be with no tracer installed.
//
// The tracer is only attached while tracing is on, and detached when it is
// off, so a normal session runs with both interpreter hooks null -- the
// same code path the game had before this milestone.

#include <string>

#include "skStatementStepper.h"
#include "skTraceCallback.h"

class skInterpreter;

namespace sk_debug {

class ScriptTracer : public skTraceCallback, public skStatementStepper {
public:
    // `MethodCalls` implies statement stepping is on too -- Simkin's method
    // trace is produced by the interpreter's own trace output, which is only
    // interesting alongside the statement counts that give it scale.
    enum class Level {
        Off,
        Statements,   // count statements and record exceptions
        MethodCalls,  // + Interpreter.Tracing, i.e. every method call logged
    };

    void Attach(skInterpreter& interpreter);
    void Detach();

    void SetLevel(Level level);
    Level level() const { return m_Level; }
    static const char* LevelName(Level level);

    // Only record trace lines containing this substring (empty = all). A
    // frame of Shadowkey script at MethodCalls produces hundreds of lines;
    // the filter is what makes the level usable at all.
    void SetFilter(std::string filter) { m_Filter = std::move(filter); }
    const std::string& filter() const { return m_Filter; }

    // skTraceCallback
    void trace(const skString& message) override;

    // skStatementStepper
    bool statementExecuted(const skStackFrame& frame, int statementType) override;
    bool compoundStatementExecuted(const skStackFrame& frame) override;
    bool exceptionEncountered(const skStackFrame* frame, const skException& e) override;
    void breakpoint(const skStackFrame* frame) override;

private:
    void ApplyLevel();

    skInterpreter* m_Interpreter = nullptr;
    Level m_Level = Level::Off;
    std::string m_Filter;
};

}  // namespace sk_debug
