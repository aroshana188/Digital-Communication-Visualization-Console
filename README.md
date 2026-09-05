# Digital Communication Visualization Console

![Hardware](https://img.shields.io/badge/Hardware-STM32_ARM_Cortex-blue?style=for-the-badge)
![PCB](https://img.shields.io/badge/PCB-Power_&_Logic-green?style=for-the-badge)
![Domain](https://img.shields.io/badge/Domain-Telecom_&_Embedded-orange?style=for-the-badge)

*An interactive embedded hardware instrument engineered to visually map and demonstrate the fundamental mathematical abstractions of digital communication pipelines.*[cite: 5]

<p align="center">
  <img src="Images/YOUR_ENCLOSURE_RENDER.png" width="70%" />
</p>

---

## 📋 Project Overview
Developed by Team SyneX for the EN2160 Electronic Design Realization module, this desktop console solves a critical pedagogical gap in undergraduate telecommunication engineering[cite: 5]. It provides a real-time, tactile hardware verification loop for abstract mathematical concepts like entropy reductions, cyclic redundancy protections, and complex constellation mappings[cite: 5].

Processing a user-defined alphabet set, the STM32-driven console dynamically renders multi-stage transformations—from Source Coding and Packetization to Channel Coding and Digital Modulation—directly onto dual 3.5" TFT panels and LED monitoring arrays[cite: 5].

---

## 🛠️ My Engineering & Business Contributions
As a core member of Team SyneX, I took ownership of the following technical and commercial domains:

### 1. Power Architecture & PCB Design
* **Dual-Buck Converter PCB:** Designed a dedicated power distribution PCB utilizing switching-mode buck converters to efficiently step down a 12V DC input to a stable 5V rail (for TFT displays/LEDs) and a clean 3.3V rail (for the STM32 logic)[cite: 5].
* **Signal Integrity:** Implemented strict isolation strategies on a double-sided FR4 PCB, restricting high-speed SPI data traces to the top copper layer while isolating sensitive analog noise-simulation routing on the bottom layer, anchored by a solid ground pour to prevent EMI[cite: 5].

### 2. Mechanical Engineering & Enclosure CAD
* **Parametric Modeling:** Engineered the mechanical enclosure using SolidWorks, adopting a top-down assembly methodology[cite: 5]. 
* **Ergonomics & Fabrication:** Designed optimized, reflection-free display mount angles for collaborative laboratory viewing[cite: 5]. Integrated specialized internal mounting bosses for the PCBs and utilized equation-driven global variables to ensure dimensional coherence for eventual injection-molding scalability[cite: 5].

### 3. User Interface (UI) Architecture
* **Dual-Display Logic:** Contributed to the interface formatting layout across the twin 3.5" SPI TFT panels[cite: 5].
* **Tactile Integration:** Mapped the interface to allow users to input character strings via a 4x4 matrix keypad and visualize real-time bit error rate (BER) transformations dynamically controlled by a physical analog linear potentiometer[cite: 5].

### 4. Business Model & Market Analysis
* **Demand Forecasting:** Evaluated the Sri Lankan educational sector, identifying a potential market demand of 80 to 100 units across local universities and technical institutes[cite: 6].
* **Financial Procurement:** Calculated a scaled mass production budget, bringing the final market-ready cost down to LKR 25,425 per unit by strategizing bulk component sourcing and injection molding[cite: 5].
* **Commercialization Strategy:** Formulated a competitive B2B pricing strategy and Unique Value Proposition, positioning the console as a highly affordable, locally manufactured alternative to expensive imported telecommunication instrumentation platforms[cite: 5].

---

## ⚙️ System Specifications & Capabilities

### Hardware Core
* **Microcontroller:** STM32 ARM Cortex-M Series[cite: 5]
* **Displays:** 2 x 3.5" SPI TFT Displays (320x480) driven via DMA[cite: 5]
* **Bitstream Mapping:** 3x 74HC595 Serial-in Parallel-out Shift Register LED Arrays[cite: 5]
* **Channel Simulation:** Analog Linear Rotational Potentiometer mapped to a 12-bit ADC[cite: 5]

### Algorithmic Pipeline
1. **Source Coding:** Real-time generation of Fixed-Length, Huffman, and Shannon-Fano encoding trees[cite: 5].
2. **Packetization:** Dynamic segmentation of bitstreams into 4, 6, or 8-bit frame boundaries[cite: 5, 6].
3. **Channel Coding:** Generation and verification of Parity (Even/Odd), CRC (CRC-3/CRC-4), and (7, 4) Hamming Code protection[cite: 5].
4. **Modulation:** Phase-mapping for ASK, BPSK, QPSK, and 16-QAM constellation rendering[cite: 5].

---

## 🤝 Team SyneX
This console was engineered by undergraduates at the Department of Electronic & Telecommunication Engineering, University of Moratuwa:
* **A.M.S. Ahamed**[cite: 5]
* **H.A.P. Aroshana** *(Power PCB, Enclosure CAD, UI Design, Market Analysis)*[cite: 5, 6]
* **W.D.A.C. Bandara**[cite: 5]
* **R.K.T. Dissanayake**[cite: 5]
* **U.G.R.B. Tennakoon**[cite: 5]
