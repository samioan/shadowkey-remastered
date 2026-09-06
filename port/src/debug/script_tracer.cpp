#include "debug/script_tracer.h"

#include "debug/debug_metrics.h"
#include "skException.h"
#include "skInterpreter.h"
#include "skStackFrame.h"

namespace sk_debug {

namespace {

std::string ToStd(const skString& s) {
    return std::string(s.ptr(), s.length());
}

// The interpreter's own "tracing" attribute. Lowercase: the header
// documents it as `Interpreter.Tracing`, but the literal skInterpreter
// actually compares against (skConstants.h's `skLITERAL(tracing)`) is
// lowercase, and skString comparison is case-sensitive.
const char* kTracingAttribute = "tracing";

}  // namespace

const char* ScriptTracer::LevelName(Level level) {
    switch (level) {
        case Level::Statements: return "statements";
        case Level::MethodCalls: return "methods";
        case Level::Off:
        default: return "off";
    }
}

void ScriptTracer::Attach(skInterpreter& interpreter) {
    m_Interpreter = &interpreter;
    ApplyLevel();
}

void ScriptTracer::Detach() {
    if (m_Interpreter) {
        m_Interpreter->setTraceCallback(nullptr);
        m_Interpreter->setStatementStepper(nullptr);
        m_Interpreter->setValue(skString(kTracingAttribute), skString(""), skRValue(false));
    }
    m_Interpreter = nullptr;
}

void ScriptTracer::SetLevel(Level level) {
    m_Level = level;
    ApplyLevel();
}

void ScriptTracer::ApplyLevel() {
    if (!m_Interpreter) return;
    // Off means genuinely off: both interpreter hooks go back to null, so
    // the interpreter runs the identical code path it ran before this
    // milestone existed. Leaving a no-op stepper installed would put a
    // virtual call in front of every statement in the game for nothing.
    if (m_Level == Level::Off) {
        m_Interpreter->setTraceCallback(nullptr);
        m_Interpreter->setStatementStepper(nullptr);
        m_Interpreter->setValue(skString(kTracingAttribute), skString(""), skRValue(false));
        return;
    }
    m_Interpreter->setTraceCallback(this);
    m_Interpreter->setStatementStepper(this);
    m_Interpreter->setValue(skString(kTracingAttribute), skString(""),
                             skRValue(m_Level == Level::MethodCalls));
}

void ScriptTracer::trace(const skString& message) {
    std::string text = ToStd(message);
    // The interpreter's own trace lines end in a newline; the event ring
    // stores one line per event, so strip it rather than storing a ragged
    // string that breaks the panel's row layout.
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) text.pop_back();
    if (text.empty()) return;
    Count("script.method_calls");
    if (!m_Filter.empty() && text.find(m_Filter) == std::string::npos) return;
    Log("script", std::move(text));
}

bool ScriptTracer::statementExecuted(const skStackFrame&, int) {
    Count("script.statements");
    // ALWAYS true. Returning false halts the running script method -- see
    // script_tracer.h. An instrument that can change control flow is not an
    // instrument.
    return true;
}

bool ScriptTracer::compoundStatementExecuted(const skStackFrame&) {
    Count("script.compound_statements");
    return true;  // see above
}

bool ScriptTracer::exceptionEncountered(const skStackFrame* frame, const skException& e) {
    Count("script.exceptions");
    std::string where;
    if (frame) {
        where = ToStd(frame->getLocation()) + ":" + std::to_string(frame->getLineNum() + 1) + " ";
    }
    Log("script-error", where + ToStd(e.toString()));
    // ALWAYS true: false would swallow the exception and let the script run
    // on, which is a behaviour change, not an observation.
    return true;
}

void ScriptTracer::breakpoint(const skStackFrame* frame) {
    Count("script.breakpoints");
    std::string where;
    if (frame) {
        where = ToStd(frame->getLocation()) + ":" + std::to_string(frame->getLineNum() + 1);
    }
    Log("script", "breakpoint at " + (where.empty() ? std::string("<unknown>") : where));
}

}  // namespace sk_debug
