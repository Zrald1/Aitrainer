# AI Trainer

AI Trainer is a Qt Quick/C++ desktop application for experimenting with a learning agent, persistent associative memory, a lightweight LoRA-style adapter, and an AI teacher simulation loop. The app provides a visual dashboard for chatting with the agent, inspecting learned associations, training from text or datasets, and running curriculum-style teacher/student evaluations.

> Status: experimental learning simulator. This project is intended for local experimentation, education, and prototype research rather than production AI deployment.

## Features

- **Interactive AI chat** with learning-enabled memory updates.
- **Teacher/student simulation** using a configurable Featherless chat-completions model.
- **Visible student thinking** with calculation checks for math questions and reasoning checks for word/concept questions.
- **Topic-based curriculum generation** where the teacher creates a lesson, a related question, and an answer.
- **Duplicate-question rejection** to reduce repeated teacher prompts during training.
- **Brain inspector dashboard** for vocabulary size, association counts, word transitions, and top reinforced associations.
- **Persistent memory files** for agent state, learned knowledge, and LoRA-like adapter weights.
- **C++ LoRA-style training** from pasted text or supported Hugging Face dataset files.
- **Agent package import/export** through `.ai` package files.
- **DigitalOcean GPU remote training bridge** that uploads the latest `.ai` package to an SSH-accessible GPU server, trains there, downloads the trained package, and imports it locally.

## Tech Stack

- **C++17**
- **Qt 6 Quick / QML**
- **Qt Network**
- **CMake**
- **Featherless AI API** for teacher and evaluator model calls

## Project Structure

```text
Aitrainer/
├── Main.qml                 # Qt Quick user interface
├── main.cpp                 # Qt application entry point
├── agentcontroller.*        # QML bridge, chat, teacher simulation, persistence
├── learningagent.*          # Student agent wrapper
├── Brain.*                  # Central cognitive loop
├── Hippocampus.*            # Persistent associative memory
├── Thalamus.*               # Input token routing
├── FrontalLobe.*            # Response generation
├── BasalGanglia.*           # Action/transition selection
├── Amygdala.*               # Salience/reward tracking
├── Cerebellum.*             # Practice counts
├── loraadapter.*            # LoRA-like adapter training
└── CMakeLists.txt           # Build configuration
```

## Requirements

- Qt **6.8+** with the following modules:
  - `Qt6::Quick`
  - `Qt6::Network`
- CMake **3.16+**
- A C++17 compiler supported by your Qt kit
- Optional: Featherless API key for teacher simulation
- Optional: Hugging Face access token for private or gated datasets
- Optional: OpenSSH `ssh`/`scp` on PATH and an SSH-accessible DigitalOcean GPU droplet for remote dataset training

## Build and Run

### Option 1: Qt Creator

1. Open `CMakeLists.txt` in Qt Creator.
2. Select a Qt 6 desktop kit.
3. Configure the project.
4. Build and run `appAitrainer`.

### Option 2: Command Line on Windows

Adjust the Qt paths to match your installed kit.

```powershell
$env:PATH='C:\Qt\Tools\mingw1310_64\bin;' + $env:PATH
cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH="C:\Qt\6.10.1\mingw_64"
cmake --build build
.\build\appAitrainer.exe
```

If your build output uses a Qt Creator kit folder, run the generated executable from that configured build directory.

## Configuration

The app stores settings with `QSettings` under:

```text
AitrainerCorp / Aitrainer
```

Inside the app, open the **AI Teacher Simulator** tab and configure:

- **API key**: Featherless API key for teacher/evaluator calls.
- **HF token**: Optional Hugging Face access token for private or gated dataset downloads.
- **Teacher model**: Chat model used to generate lessons, questions, answers, and evaluations.
- **Topic**: Curriculum topic the teacher should obey.
- **Delay**: Simulation pacing between turns.
- **GPU host/user/key/remote dir**: DigitalOcean GPU server connection settings for remote package training.

Do not commit real API keys to GitHub. Configure secrets locally through the app UI or your local environment.

## Runtime Files

The app may create or update these local files while running:

```text
agent_memory.txt
sentence_memory.txt
student_knowledge.json
lora_adapter.txt
note.txt
student_agent.ai
qml_errors.txt
```

These files represent generated memory, training output, exports, logs, or notes. Review them before committing, especially if they contain private training text or API-derived content.

## Training Workflow

1. Start the app.
2. Configure the Featherless API key and teacher model.
3. Enter a curriculum topic, such as `Writing Equations`.
4. Start the simulation.
5. The teacher creates a lesson, question, and answer.
6. The student agent answers with visible thinking.
7. The evaluator scores the answer and thinking.
8. Incorrect answers trigger correction and re-teaching.
9. Learned facts and associations are persisted locally.

## Agent Thinking Behavior

The student agent is designed to show how it checks an answer:

- Math questions use calculation-focused thinking, for example:

```text
[Thinking: Calculation check: I use the operation from the question: 12 * 4 = 48.]
Answer: 48
```

- Word or concept questions use reasoning-focused thinking, for example:

```text
[Thinking: Answer check: I read what the question is asking, isolate the required kind of answer, and check that the answer directly satisfies it.]
Answer: ...
```

The local student also keeps a lightweight sentence memory. Training text and datasets are converted into complete sentence examples where possible, so fallback answers can use readable English sentences instead of only raw word-transition chains.

## Dataset Training

The LoRA-style trainer can train from:

- Pasted examples, corrections, Q&A, or skill text.
- Hugging Face dataset repo IDs or dataset file URLs.
- Local dataset files or direct URLs when the content is text-like.
- Supported text-like files: `.txt`, `.md`, `.json`, `.jsonl`, `.csv`, `.tsv`, `.xml`, `.html`, `.yaml`, `.yml`.
- Hugging Face datasets backed by Parquet/Arrow through the Hugging Face Dataset Viewer rows API.

For private or gated Hugging Face datasets, paste a read-capable token into **HF token** before clicking **Train Dataset**. The app sends the token only to Hugging Face hosts and does not include it in logs, exports, or package status text.

The loader tries to convert datasets into structured samples before training:

```text
Question: ...
Lesson: ...
Answer: ...
```

It understands common instruction, QA, and chat schemas such as `question`/`answer`, `instruction`/`output`, `prompt`/`completion`, `messages`, `conversations`, SQuAD-style `paragraphs/qas`, and CSV/TSV columns with matching names. If a dataset row has different column names, the loader now infers useful training pairs from the row fields, such as `text + label`, `title + description`, or generic field lookup questions.

Dataset training is chunked so the UI stays responsive. Binary files that cannot be converted to text or Dataset Viewer rows are rejected instead of being learned as corrupted text.

## DigitalOcean GPU Remote Training

The remote training bridge is for machines that cannot download or process large datasets locally. It uses an existing DigitalOcean GPU server over SSH:

1. The desktop app exports the current student state as the latest `.ai` package.
2. The app uploads only that package and a small remote trainer script with `scp`.
3. The GPU server downloads the dataset or Hugging Face repo, updates the package memory/knowledge files, and writes `trained.ai`.
4. The desktop app downloads `trained.ai` and imports it back into the local student model.

For GPU training, the dataset field should be a Hugging Face repo ID, a direct dataset URL, or a file path that already exists on the GPU server. Local Windows dataset files are not uploaded because the remote path is designed to keep large dataset downloads off the desktop machine.

Server requirements:

- A running DigitalOcean GPU droplet reachable by SSH.
- Python 3 and `pip` on the server.
- Network access from the server to the dataset source.
- A Hugging Face token in the app settings when the remote dataset is private or gated.

The current bridge trains this project's `.ai` package format: associative memory, sentence memory, structured knowledge, and notes. It does not create a full transformer checkpoint or PEFT adapter by itself. If you add a transformer training script later, this SSH/SCP bridge can be reused as the transfer and orchestration layer.

## Export and Import

Use the package controls to export or import a student agent package:

```text
student_agent.ai
```

The package includes memory-related files and checksums so an agent state can be moved between local copies of the app.

## Mobile App

The `aimobile/` target is a Qt Quick mobile chat app for using exported student models locally on a phone. Export `student_agent.ai` from the desktop trainer, transfer it to the phone, open the mobile app, and use **Import .ai**. The mobile app unpacks the compressed package into its writable app data folder, verifies file checksums, loads the local student memory, and chats without calling the teacher API.

The mobile importer accepts the same `.ai` package files created by the desktop app and restores:

```text
agent_memory.txt
sentence_memory.txt
student_knowledge.json
lora_adapter.txt
note.txt
```

## Development Notes

- The app uses a local biological metaphor for memory and response generation.
- The LoRA adapter is a lightweight C++ simulation, not a full neural-network PEFT implementation.
- Teacher and evaluator quality depends on the configured external model.
- Generated training content should be reviewed before being used as reliable educational material.

## License

No license file is currently included. Add a license before publishing if you want others to use, modify, or distribute the project.
