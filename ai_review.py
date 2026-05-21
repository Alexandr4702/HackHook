from crewai import Agent, Task, Crew, LLM

# ─────────────────────────────
# LLM (Ollama)
# ─────────────────────────────
llm = LLM(
    model="ollama/qwen2.5-coder:14b",
    base_url="http://localhost:11434"
)

# ─────────────────────────────
# Architect Agent
# ─────────────────────────────
architect = Agent(
    role="Software Architect",
    goal="Analyze C++ subsystem architecture and identify structural problems",
    backstory="Senior C++ systems engineer focused on clean modular design",
    llm=llm,
    verbose=True
)

# ─────────────────────────────
# Reviewer Agent
# ─────────────────────────────
reviewer = Agent(
    role="C++ Code Reviewer",
    goal="Find bugs, unsafe patterns, memory issues, threading problems",
    backstory="Senior C++ engineer specializing in performance and safety",
    llm=llm,
    verbose=True
)

# ─────────────────────────────
# Task 1: Architecture review
# ─────────────────────────────
architecture_task = Task(
    description="""
You are reviewing a C++ memory scanning subsystem.

Focus on these files:
- myhook/MemoryScanner.cpp
- myhook/MemoryScanner.h

Analyze:
- module boundaries
- coupling with other components
- API design
- responsibilities separation
""",
    expected_output="""
A structured architecture review including:
- identified problems
- severity (low/medium/high)
- suggestions for improvement
""",
    agent=architect
)

# ─────────────────────────────
# Task 2: Deep review
# ─────────────────────────────
review_task = Task(
    description="""
Take the architecture review and perform a deeper code-level analysis.

Focus on:
- memory safety (ownership, leaks)
- threading issues
- undefined behavior risks
- C++ best practices violations

Provide concrete fixes or patterns.
""",
    expected_output="""
A detailed technical review with:
- concrete issues
- severity levels
- recommended fixes in C++
""",
    agent=reviewer
)

# ─────────────────────────────
# Crew pipeline
# ─────────────────────────────
crew = Crew(
    agents=[architect, reviewer],
    tasks=[architecture_task, review_task],
    verbose=True
)

# ─────────────────────────────
# Run
# ─────────────────────────────
if __name__ == "__main__":
    result = crew.kickoff()
    print("\n\n===== FINAL RESULT =====\n")
    print(result)