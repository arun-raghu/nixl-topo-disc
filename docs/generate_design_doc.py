#!/usr/bin/env python3
"""
NIXL Topology Discovery System - Design Document Generator
Creates a visual-heavy PDF with block diagrams, sequence diagrams, and architecture overview.
"""

from reportlab.lib import colors
from reportlab.lib.pagesizes import letter
from reportlab.lib.styles import getSampleStyleSheet, ParagraphStyle
from reportlab.lib.units import inch
from reportlab.platypus import SimpleDocTemplate, Paragraph, Spacer, Table, TableStyle, PageBreak, Image, KeepTogether
from reportlab.graphics.shapes import Drawing, Rect, String, Line, Polygon
from reportlab.graphics import renderPDF
from reportlab.lib.enums import TA_CENTER, TA_LEFT
import matplotlib.pyplot as plt
import matplotlib.patches as mpatches
from matplotlib.patches import FancyBboxPatch, FancyArrowPatch, Circle
import numpy as np
import io

# Page setup
PAGE_WIDTH, PAGE_HEIGHT = letter

def create_component_diagram():
    """Create system architecture block diagram"""
    fig, ax = plt.subplots(1, 1, figsize=(10, 6))
    ax.set_xlim(0, 10)
    ax.set_ylim(0, 7)
    ax.axis('off')

    # Controller box
    controller = FancyBboxPatch((3.5, 5), 3, 1.5, boxstyle="round,pad=0.05",
                                 facecolor='#4A90D9', edgecolor='black', linewidth=2)
    ax.add_patch(controller)
    ax.text(5, 5.75, 'CONTROLLER', ha='center', va='center', fontsize=12, fontweight='bold', color='white')
    ax.text(5, 5.35, 'Test Manager | Orchestrator', ha='center', va='center', fontsize=8, color='white')

    # Controller Buffer
    buffer_ctrl = FancyBboxPatch((7, 5.2), 2.5, 1, boxstyle="round,pad=0.03",
                                  facecolor='#FFD700', edgecolor='black', linewidth=1.5)
    ax.add_patch(buffer_ctrl)
    ax.text(8.25, 5.7, 'Controller Buffer', ha='center', va='center', fontsize=9, fontweight='bold')
    ax.text(8.25, 5.4, '(NIXL Registered)', ha='center', va='center', fontsize=7)

    # Agents row
    agent_colors = ['#50C878', '#50C878', '#50C878', '#50C878']
    agent_positions = [(0.5, 2), (2.5, 2), (4.5, 2), (6.5, 2)]

    for i, (pos, color) in enumerate(zip(agent_positions, agent_colors)):
        agent = FancyBboxPatch(pos, 1.8, 1.2, boxstyle="round,pad=0.05",
                               facecolor=color, edgecolor='black', linewidth=2)
        ax.add_patch(agent)
        ax.text(pos[0]+0.9, pos[1]+0.75, f'Agent {i}', ha='center', va='center',
                fontsize=10, fontweight='bold', color='white')
        ax.text(pos[0]+0.9, pos[1]+0.35, 'Test Buffer', ha='center', va='center',
                fontsize=8, color='white')

    # More agents indicator
    ax.text(9, 2.6, '...', ha='center', va='center', fontsize=20, fontweight='bold')
    agent_n = FancyBboxPatch((8.5, 2), 1.2, 1.2, boxstyle="round,pad=0.05",
                              facecolor='#50C878', edgecolor='black', linewidth=2)
    ax.add_patch(agent_n)
    ax.text(9.1, 2.6, f'Agent N', ha='center', va='center', fontsize=9, fontweight='bold', color='white')

    # NIXL Layer
    nixl_layer = FancyBboxPatch((0.3, 3.5), 9.4, 0.8, boxstyle="round,pad=0.02",
                                 facecolor='#FF6B6B', edgecolor='black', linewidth=2, alpha=0.8)
    ax.add_patch(nixl_layer)
    ax.text(5, 3.9, 'NIXL RDMA Transport Layer (UCX)', ha='center', va='center',
            fontsize=11, fontweight='bold', color='white')

    # Arrows from controller to NIXL layer
    ax.annotate('', xy=(5, 4.3), xytext=(5, 5),
                arrowprops=dict(arrowstyle='->', color='black', lw=2))

    # Arrows from NIXL to agents
    for pos in agent_positions:
        ax.annotate('', xy=(pos[0]+0.9, 3.2), xytext=(pos[0]+0.9, 3.5),
                    arrowprops=dict(arrowstyle='<->', color='black', lw=1.5))
    ax.annotate('', xy=(9.1, 3.2), xytext=(9.1, 3.5),
                arrowprops=dict(arrowstyle='<->', color='black', lw=1.5))

    # Container Orchestrator
    orch = FancyBboxPatch((0.3, 5.5), 2.5, 0.8, boxstyle="round,pad=0.03",
                           facecolor='#9B59B6', edgecolor='black', linewidth=1.5)
    ax.add_patch(orch)
    ax.text(1.55, 5.9, 'Orchestrator', ha='center', va='center', fontsize=9, fontweight='bold', color='white')
    ax.text(1.55, 5.6, '(Docker/K8s)', ha='center', va='center', fontsize=7, color='white')

    # Arrow from orchestrator to controller
    ax.annotate('', xy=(3.5, 5.9), xytext=(2.8, 5.9),
                arrowprops=dict(arrowstyle='->', color='black', lw=1.5))

    # Legend
    ax.text(0.5, 0.8, 'Key:', fontsize=9, fontweight='bold')
    ax.add_patch(FancyBboxPatch((0.5, 0.2), 0.4, 0.3, facecolor='#4A90D9'))
    ax.text(1.1, 0.35, 'Controller', fontsize=8)
    ax.add_patch(FancyBboxPatch((2.2, 0.2), 0.4, 0.3, facecolor='#50C878'))
    ax.text(2.8, 0.35, 'Agents', fontsize=8)
    ax.add_patch(FancyBboxPatch((3.9, 0.2), 0.4, 0.3, facecolor='#FF6B6B'))
    ax.text(4.5, 0.35, 'NIXL/RDMA', fontsize=8)
    ax.add_patch(FancyBboxPatch((5.8, 0.2), 0.4, 0.3, facecolor='#FFD700'))
    ax.text(6.4, 0.35, 'Shared Buffer', fontsize=8)

    plt.title('System Architecture - Component Overview', fontsize=14, fontweight='bold', pad=20)
    plt.tight_layout()

    buf = io.BytesIO()
    plt.savefig(buf, format='png', dpi=150, bbox_inches='tight')
    buf.seek(0)
    plt.close()
    return buf


def create_buffer_layout_diagram():
    """Create controller buffer layout diagram with write ownership arrows"""
    fig, ax = plt.subplots(1, 1, figsize=(11, 5.5))
    ax.set_xlim(0, 12)
    ax.set_ylim(0, 6)
    ax.axis('off')

    # Main buffer outline
    main = FancyBboxPatch((0.5, 0.5), 8, 5, boxstyle="round,pad=0.02",
                           facecolor='white', edgecolor='black', linewidth=2)
    ax.add_patch(main)
    ax.text(4.5, 5.7, 'Controller Buffer (NIXL Registered Memory)',
            ha='center', va='center', fontsize=12, fontweight='bold')

    # Header section
    header = FancyBboxPatch((0.7, 4.8), 7.6, 0.5, boxstyle="square",
                             facecolor='#E8E8E8', edgecolor='black', linewidth=1)
    ax.add_patch(header)
    ax.text(4.5, 5.05, 'HEADER: num_agents | offsets | ready_flag',
            ha='center', va='center', fontsize=9)

    # Agent metadata slots
    metadata = FancyBboxPatch((0.7, 3.8), 7.6, 0.8, boxstyle="square",
                               facecolor='#B8D4E8', edgecolor='black', linewidth=1)
    ax.add_patch(metadata)
    ax.text(4.5, 4.2, 'AGENT METADATA SLOTS', ha='center', va='center', fontsize=10, fontweight='bold')

    # Small boxes for slots
    for i in range(5):
        slot = FancyBboxPatch((0.9 + i*1.5, 3.9), 1.3, 0.3, boxstyle="square",
                               facecolor='#87CEEB', edgecolor='black', linewidth=0.5)
        ax.add_patch(slot)
        ax.text(1.55 + i*1.5, 4.05, f'Slot {i}', ha='center', va='center', fontsize=7)

    # Test control region
    test_ctrl = FancyBboxPatch((0.7, 2.5), 7.6, 1.1, boxstyle="square",
                                facecolor='#FFE4B5', edgecolor='black', linewidth=1)
    ax.add_patch(test_ctrl)
    ax.text(4.5, 3.35, 'TEST CONTROL REGION', ha='center', va='center', fontsize=10, fontweight='bold')

    # Command box
    cmd = FancyBboxPatch((0.9, 2.6), 3.2, 0.5, boxstyle="square",
                          facecolor='#FFD700', edgecolor='black', linewidth=0.5)
    ax.add_patch(cmd)
    ax.text(2.5, 2.85, 'TestCommand', ha='center', va='center', fontsize=8)

    # Status slots
    status = FancyBboxPatch((4.3, 2.6), 3.8, 0.5, boxstyle="square",
                             facecolor='#98FB98', edgecolor='black', linewidth=0.5)
    ax.add_patch(status)
    ax.text(6.2, 2.85, 'Agent Status Slots [0..N-1]', ha='center', va='center', fontsize=8)

    # Results region
    results = FancyBboxPatch((0.7, 0.7), 7.6, 1.4, boxstyle="square",
                              facecolor='#F0E68C', edgecolor='black', linewidth=1)
    ax.add_patch(results)
    ax.text(4.5, 1.4, 'RESULTS REGION (128 bytes per agent)', ha='center', va='center', fontsize=9, fontweight='bold')

    # === RIGHT SIDE: Write ownership arrows and labels ===
    arrow_x = 8.5
    label_x = 9.2

    # Header - Controller writes
    ax.annotate('', xy=(arrow_x, 5.05), xytext=(arrow_x + 0.5, 5.05),
                arrowprops=dict(arrowstyle='->', color='#4A90D9', lw=2))
    ax.text(label_x + 0.6, 5.05, 'Controller writes', ha='left', va='center',
            fontsize=8, fontweight='bold', color='#4A90D9')

    # Agent metadata - Agents write
    ax.annotate('', xy=(arrow_x, 4.2), xytext=(arrow_x + 0.5, 4.2),
                arrowprops=dict(arrowstyle='->', color='#50C878', lw=2))
    ax.text(label_x + 0.6, 4.2, 'Agents write', ha='left', va='center',
            fontsize=8, fontweight='bold', color='#50C878')
    ax.text(label_x + 0.6, 3.95, '(endpoint + buffer)', ha='left', va='center',
            fontsize=7, color='#50C878')

    # TestCommand - Controller writes
    ax.annotate('', xy=(arrow_x, 3.0), xytext=(arrow_x + 0.5, 3.0),
                arrowprops=dict(arrowstyle='->', color='#4A90D9', lw=2))
    ax.text(label_x + 0.6, 3.0, 'Controller writes', ha='left', va='center',
            fontsize=8, fontweight='bold', color='#4A90D9')
    ax.text(label_x + 0.6, 2.75, '(commands)', ha='left', va='center',
            fontsize=7, color='#4A90D9')

    # Agent Status - Agents write (with offset arrow)
    ax.annotate('', xy=(arrow_x, 2.5), xytext=(arrow_x + 0.5, 2.5),
                arrowprops=dict(arrowstyle='->', color='#50C878', lw=2))
    ax.text(label_x + 0.6, 2.5, 'Agents write', ha='left', va='center',
            fontsize=8, fontweight='bold', color='#50C878')
    ax.text(label_x + 0.6, 2.25, '(status updates)', ha='left', va='center',
            fontsize=7, color='#50C878')

    # Results - Agents write
    ax.annotate('', xy=(arrow_x, 1.4), xytext=(arrow_x + 0.5, 1.4),
                arrowprops=dict(arrowstyle='->', color='#50C878', lw=2))
    ax.text(label_x + 0.6, 1.4, 'Agents write', ha='left', va='center',
            fontsize=8, fontweight='bold', color='#50C878')
    ax.text(label_x + 0.6, 1.15, '(test results)', ha='left', va='center',
            fontsize=7, color='#50C878')

    # Legend at bottom
    ax.add_patch(FancyBboxPatch((0.5, 0.1), 0.3, 0.2, facecolor='#4A90D9'))
    ax.text(1.0, 0.2, 'Controller', fontsize=7, va='center')
    ax.add_patch(FancyBboxPatch((2.2, 0.1), 0.3, 0.2, facecolor='#50C878'))
    ax.text(2.7, 0.2, 'Agents', fontsize=7, va='center')

    plt.tight_layout()
    buf = io.BytesIO()
    plt.savefig(buf, format='png', dpi=150, bbox_inches='tight')
    buf.seek(0)
    plt.close()
    return buf


def create_agent_buffer_layout_diagram():
    """Create agent buffer layout diagram"""
    fig, ax = plt.subplots(1, 1, figsize=(10, 4))
    ax.set_xlim(0, 12)
    ax.set_ylim(0, 5)
    ax.axis('off')

    # Main buffer outline
    main = FancyBboxPatch((0.5, 0.5), 9, 4, boxstyle="round,pad=0.02",
                           facecolor='white', edgecolor='black', linewidth=2)
    ax.add_patch(main)
    ax.text(5, 4.7, 'Agent Buffer (256 MB NIXL Registered Memory)',
            ha='center', va='center', fontsize=12, fontweight='bold')

    # Transfer slots (largest region)
    transfer = FancyBboxPatch((0.7, 1.2), 8.6, 2.8, boxstyle="square",
                               facecolor='#90EE90', edgecolor='black', linewidth=1)
    ax.add_patch(transfer)
    ax.text(5, 2.9, 'TRANSFER SLOTS', ha='center', va='center', fontsize=11, fontweight='bold')
    ax.text(5, 2.4, '~256 MB - 8KB', ha='center', va='center', fontsize=9)
    ax.text(5, 2.0, '64 slots x 4MB max (for bandwidth tests)', ha='center', va='center', fontsize=8)
    ax.text(5, 1.6, 'Peer agents write data here via RDMA', ha='center', va='center', fontsize=8, style='italic')

    # Command inbox
    cmd = FancyBboxPatch((0.7, 0.7), 4.2, 0.4, boxstyle="square",
                          facecolor='#FFD700', edgecolor='black', linewidth=1)
    ax.add_patch(cmd)
    ax.text(2.8, 0.9, 'COMMAND INBOX (4 KB)', ha='center', va='center', fontsize=9, fontweight='bold')

    # Mailbox
    mailbox = FancyBboxPatch((5.1, 0.7), 4.2, 0.4, boxstyle="square",
                              facecolor='#87CEEB', edgecolor='black', linewidth=1)
    ax.add_patch(mailbox)
    ax.text(7.2, 0.9, 'MAILBOX (4 KB)', ha='center', va='center', fontsize=9, fontweight='bold')

    # Right side: Write ownership
    ax.text(10, 3.5, 'Writers:', fontsize=9, fontweight='bold')
    ax.add_patch(FancyBboxPatch((10, 3.0), 0.3, 0.25, facecolor='#90EE90'))
    ax.text(10.5, 3.1, 'Peer agents', fontsize=8, va='center')
    ax.add_patch(FancyBboxPatch((10, 2.5), 0.3, 0.25, facecolor='#FFD700'))
    ax.text(10.5, 2.6, 'Controller', fontsize=8, va='center')
    ax.add_patch(FancyBboxPatch((10, 2.0), 0.3, 0.25, facecolor='#87CEEB'))
    ax.text(10.5, 2.1, 'Peer (ping-pong)', fontsize=8, va='center')

    # Size annotations
    ax.text(5, 0.3, 'Total: 256 MB | Transfer: window_size x msg_size | Cmd+Mailbox: 8 KB',
            ha='center', fontsize=8, color='gray')

    plt.tight_layout()
    buf = io.BytesIO()
    plt.savefig(buf, format='png', dpi=150, bbox_inches='tight')
    buf.seek(0)
    plt.close()
    return buf


def create_sequence_diagram_bootstrap():
    """Create bootstrap sequence diagram"""
    fig, ax = plt.subplots(1, 1, figsize=(10, 7))
    ax.set_xlim(0, 10)
    ax.set_ylim(0, 10)
    ax.axis('off')

    # Title
    ax.text(5, 9.7, 'Bootstrap & Rendezvous Sequence', ha='center', fontsize=14, fontweight='bold')

    # Actor labels
    actors = [('Orchestrator', 1), ('Controller', 3.5), ('Agent 0', 6), ('Agent N', 8.5)]
    for name, x in actors:
        ax.text(x, 9.2, name, ha='center', fontsize=10, fontweight='bold')
        ax.plot([x, x], [0.5, 9], 'k--', linewidth=1, alpha=0.5)

    # Step numbers and messages
    y = 8.5
    step = 0.8

    # 1. Controller init
    ax.text(0.2, y, '1', fontsize=9, fontweight='bold', color='blue')
    ax.add_patch(FancyBboxPatch((2.8, y-0.15), 1.4, 0.35, facecolor='#B8D4E8'))
    ax.text(3.5, y, 'Init NIXL', ha='center', fontsize=8)

    # 2. Spawn agents
    y -= step
    ax.text(0.2, y, '2', fontsize=9, fontweight='bold', color='blue')
    ax.annotate('', xy=(6, y), xytext=(3.5, y),
                arrowprops=dict(arrowstyle='->', color='green', lw=1.5))
    ax.text(4.75, y+0.15, 'spawn(env: CTRL_ENDPOINT, AGENT_ID)', fontsize=7, ha='center')

    # 3. Agent init
    y -= step
    ax.text(0.2, y, '3', fontsize=9, fontweight='bold', color='blue')
    ax.add_patch(FancyBboxPatch((5.3, y-0.15), 1.4, 0.35, facecolor='#98FB98'))
    ax.text(6, y, 'Parse env', ha='center', fontsize=8)

    # 4. Connect NIXL
    y -= step
    ax.text(0.2, y, '4', fontsize=9, fontweight='bold', color='blue')
    ax.add_patch(FancyBboxPatch((5.3, y-0.15), 1.4, 0.35, facecolor='#98FB98'))
    ax.text(6, y, 'Connect NIXL', ha='center', fontsize=8)

    # 5. Write metadata
    y -= step
    ax.text(0.2, y, '5', fontsize=9, fontweight='bold', color='blue')
    ax.annotate('', xy=(3.5, y), xytext=(6, y),
                arrowprops=dict(arrowstyle='->', color='red', lw=1.5))
    ax.text(4.75, y+0.15, 'NIXL Write: endpoint + buffer blob', fontsize=7, ha='center', color='red')

    # 6. Poll for all agents
    y -= step
    ax.text(0.2, y, '6', fontsize=9, fontweight='bold', color='blue')
    ax.add_patch(FancyBboxPatch((2.8, y-0.15), 1.4, 0.35, facecolor='#B8D4E8'))
    ax.text(3.5, y, 'Wait all', ha='center', fontsize=8)

    # 7. Notify rendezvous complete
    y -= step
    ax.text(0.2, y, '7', fontsize=9, fontweight='bold', color='blue')
    ax.annotate('', xy=(6, y), xytext=(3.5, y),
                arrowprops=dict(arrowstyle='->', color='red', lw=1.5))
    ax.annotate('', xy=(8.5, y), xytext=(3.5, y),
                arrowprops=dict(arrowstyle='->', color='red', lw=1.5))
    ax.text(5.5, y+0.15, 'NIXL Notify: RENDEZVOUS_COMPLETE', fontsize=7, ha='center', color='red')

    # 8. Read peer metadata
    y -= step
    ax.text(0.2, y, '8', fontsize=9, fontweight='bold', color='blue')
    ax.add_patch(FancyBboxPatch((5.3, y-0.15), 1.4, 0.35, facecolor='#98FB98'))
    ax.text(6, y, 'Read peers', ha='center', fontsize=8)
    ax.add_patch(FancyBboxPatch((7.8, y-0.15), 1.4, 0.35, facecolor='#98FB98'))
    ax.text(8.5, y, 'Read peers', ha='center', fontsize=8)

    # 9. Event loop
    y -= step
    ax.text(0.2, y, '9', fontsize=9, fontweight='bold', color='blue')
    ax.add_patch(FancyBboxPatch((5.3, y-0.15), 1.4, 0.35, facecolor='#98FB98'))
    ax.text(6, y, 'Event loop', ha='center', fontsize=8)
    ax.add_patch(FancyBboxPatch((7.8, y-0.15), 1.4, 0.35, facecolor='#98FB98'))
    ax.text(8.5, y, 'Event loop', ha='center', fontsize=8)

    # Legend
    ax.text(0.3, 0.3, 'Legend:', fontsize=8, fontweight='bold')
    ax.annotate('', xy=(1.8, 0.3), xytext=(1.2, 0.3),
                arrowprops=dict(arrowstyle='->', color='green', lw=1.5))
    ax.text(2, 0.3, 'Orchestrator API', fontsize=7)
    ax.annotate('', xy=(4.3, 0.3), xytext=(3.7, 0.3),
                arrowprops=dict(arrowstyle='->', color='red', lw=1.5))
    ax.text(4.5, 0.3, 'NIXL Transfer', fontsize=7)

    plt.tight_layout()
    buf = io.BytesIO()
    plt.savefig(buf, format='png', dpi=150, bbox_inches='tight')
    buf.seek(0)
    plt.close()
    return buf


def create_sequence_diagram_test():
    """Create test execution sequence diagram"""
    fig, ax = plt.subplots(1, 1, figsize=(10, 6))
    ax.set_xlim(0, 10)
    ax.set_ylim(0, 8)
    ax.axis('off')

    # Title
    ax.text(5, 7.7, 'Test Execution Sequence (PAIRWISE_LATENCY)', ha='center', fontsize=14, fontweight='bold')

    # Actor labels
    actors = [('Controller', 2), ('Agent A', 5), ('Agent B', 8)]
    for name, x in actors:
        ax.text(x, 7.2, name, ha='center', fontsize=10, fontweight='bold')
        ax.plot([x, x], [0.5, 7], 'k--', linewidth=1, alpha=0.5)

    y = 6.5
    step = 0.7

    # 1. CONFIGURE
    ax.text(0.2, y, '1', fontsize=9, fontweight='bold', color='blue')
    ax.annotate('', xy=(5, y), xytext=(2, y),
                arrowprops=dict(arrowstyle='->', color='red', lw=1.5))
    ax.annotate('', xy=(8, y), xytext=(2, y),
                arrowprops=dict(arrowstyle='->', color='red', lw=1.5))
    ax.text(4, y+0.2, 'CONFIGURE + Notify', fontsize=8, color='red')

    # 2. Status READY
    y -= step
    ax.text(0.2, y, '2', fontsize=9, fontweight='bold', color='blue')
    ax.annotate('', xy=(2, y), xytext=(5, y),
                arrowprops=dict(arrowstyle='->', color='green', lw=1.5))
    ax.annotate('', xy=(2, y-0.1), xytext=(8, y-0.1),
                arrowprops=dict(arrowstyle='->', color='green', lw=1.5))
    ax.text(4, y+0.2, 'Status: READY', fontsize=8, color='green')

    # 3. START
    y -= step
    ax.text(0.2, y, '3', fontsize=9, fontweight='bold', color='blue')
    ax.annotate('', xy=(5, y), xytext=(2, y),
                arrowprops=dict(arrowstyle='->', color='red', lw=1.5))
    ax.annotate('', xy=(8, y), xytext=(2, y),
                arrowprops=dict(arrowstyle='->', color='red', lw=1.5))
    ax.text(4, y+0.2, 'START pair (A,B)', fontsize=8, color='red')

    # 4. Ping-pong
    y -= step
    ax.text(0.2, y, '4', fontsize=9, fontweight='bold', color='blue')
    ax.add_patch(FancyBboxPatch((4.3, y-0.6), 4.4, 1, boxstyle="round,pad=0.02",
                                 facecolor='#FFFACD', edgecolor='orange', linewidth=2))
    ax.annotate('', xy=(8, y), xytext=(5, y),
                arrowprops=dict(arrowstyle='->', color='purple', lw=1.5))
    ax.annotate('', xy=(5, y-0.3), xytext=(8, y-0.3),
                arrowprops=dict(arrowstyle='->', color='purple', lw=1.5))
    ax.text(6.5, y+0.25, 'Ping-Pong x10K', fontsize=8, fontweight='bold')

    # 5. Status DONE
    y -= step - 0.2
    ax.text(0.2, y, '5', fontsize=9, fontweight='bold', color='blue')
    ax.annotate('', xy=(2, y), xytext=(5, y),
                arrowprops=dict(arrowstyle='->', color='green', lw=1.5))
    ax.annotate('', xy=(2, y-0.1), xytext=(8, y-0.1),
                arrowprops=dict(arrowstyle='->', color='green', lw=1.5))
    ax.text(4, y+0.2, 'Status: DONE', fontsize=8, color='green')

    # 6. COLLECT
    y -= step
    ax.text(0.2, y, '6', fontsize=9, fontweight='bold', color='blue')
    ax.annotate('', xy=(5, y), xytext=(2, y),
                arrowprops=dict(arrowstyle='->', color='red', lw=1.5))
    ax.text(3.5, y+0.2, 'COLLECT', fontsize=8, color='red')

    # 7. Upload results
    y -= step
    ax.text(0.2, y, '7', fontsize=9, fontweight='bold', color='blue')
    ax.annotate('', xy=(2, y), xytext=(5, y),
                arrowprops=dict(arrowstyle='->', color='blue', lw=1.5))
    ax.text(3.5, y+0.2, 'Results (initiator)', fontsize=8, color='blue')

    plt.tight_layout()
    buf = io.BytesIO()
    plt.savefig(buf, format='png', dpi=150, bbox_inches='tight')
    buf.seek(0)
    plt.close()
    return buf


def create_sequence_ping_pong():
    """Create ping-pong latency test sequence diagram between two agents"""
    fig, ax = plt.subplots(1, 1, figsize=(10, 6))
    ax.set_xlim(0, 10)
    ax.set_ylim(0, 8)
    ax.axis('off')

    ax.text(5, 7.7, 'Ping-Pong Latency Test (Agent-to-Agent)', ha='center', fontsize=14, fontweight='bold')

    # Actor labels
    actors = [('Controller', 1.5), ('Agent A\n(Initiator)', 5), ('Agent B\n(Responder)', 8.5)]
    for name, x in actors:
        ax.text(x, 7.2, name, ha='center', fontsize=10, fontweight='bold')
        ax.plot([x, x], [0.5, 6.8], 'k--', linewidth=1, alpha=0.5)

    y = 6.3
    step = 0.65

    # 1. Controller sends command to both
    ax.text(0.2, y, '1', fontsize=9, fontweight='bold', color='blue')
    ax.annotate('', xy=(5, y), xytext=(1.5, y),
                arrowprops=dict(arrowstyle='->', color='red', lw=1.5))
    ax.annotate('', xy=(8.5, y-0.1), xytext=(1.5, y-0.1),
                arrowprops=dict(arrowstyle='->', color='red', lw=1.5))
    ax.text(3.25, y+0.2, 'RDMA Write: TestCommand', fontsize=7, color='red')

    # 2. Controller notifies both
    y -= step
    ax.text(0.2, y, '2', fontsize=9, fontweight='bold', color='blue')
    ax.annotate('', xy=(5, y), xytext=(1.5, y),
                arrowprops=dict(arrowstyle='->', color='orange', lw=1.5))
    ax.annotate('', xy=(8.5, y), xytext=(1.5, y),
                arrowprops=dict(arrowstyle='->', color='orange', lw=1.5))
    ax.text(4, y+0.2, 'NIXL genNotif()', fontsize=7, color='orange')

    # Ping-pong loop box
    y -= step * 0.5
    ax.add_patch(FancyBboxPatch((4.2, y-2.2), 5, 2.4, boxstyle="round,pad=0.02",
                                 facecolor='#FFFACD', edgecolor='orange', linewidth=2))
    ax.text(6.7, y, 'loop [iterations]', fontsize=8, fontweight='bold', color='orange')

    # 3. Ping: A writes to B's mailbox
    y -= step
    ax.text(0.2, y, '3', fontsize=9, fontweight='bold', color='blue')
    ax.annotate('', xy=(8.5, y), xytext=(5, y),
                arrowprops=dict(arrowstyle='->', color='purple', lw=1.5))
    ax.text(6.75, y+0.18, 'RDMA Write: ping (seq++)', fontsize=7, color='purple')

    # 4. B polls, sees seq increment
    y -= step
    ax.text(0.2, y, '4', fontsize=9, fontweight='bold', color='blue')
    ax.add_patch(FancyBboxPatch((7.8, y-0.15), 1.4, 0.35, facecolor='#98FB98'))
    ax.text(8.5, y, 'Poll mailbox', ha='center', fontsize=8)

    # 5. Pong: B writes to A's mailbox
    y -= step
    ax.text(0.2, y, '5', fontsize=9, fontweight='bold', color='blue')
    ax.annotate('', xy=(5, y), xytext=(8.5, y),
                arrowprops=dict(arrowstyle='->', color='purple', lw=1.5))
    ax.text(6.75, y+0.18, 'RDMA Write: pong (seq++)', fontsize=7, color='purple')

    # 6. A polls, measures RTT
    y -= step
    ax.text(0.2, y, '6', fontsize=9, fontweight='bold', color='blue')
    ax.add_patch(FancyBboxPatch((4.3, y-0.15), 1.4, 0.35, facecolor='#98FB98'))
    ax.text(5, y, 'Measure RTT', ha='center', fontsize=8)

    # 7. A writes results to controller
    y -= step * 1.3
    ax.text(0.2, y, '7', fontsize=9, fontweight='bold', color='blue')
    ax.annotate('', xy=(1.5, y), xytext=(5, y),
                arrowprops=dict(arrowstyle='->', color='blue', lw=1.5))
    ax.text(3.25, y+0.18, 'RDMA Write: TestResult', fontsize=7, color='blue')

    # Legend
    ax.text(0.3, 0.3, 'Legend:', fontsize=8, fontweight='bold')
    ax.annotate('', xy=(1.5, 0.3), xytext=(1, 0.3),
                arrowprops=dict(arrowstyle='->', color='red', lw=1.5))
    ax.text(1.7, 0.3, 'Command', fontsize=7)
    ax.annotate('', xy=(3.2, 0.3), xytext=(2.7, 0.3),
                arrowprops=dict(arrowstyle='->', color='purple', lw=1.5))
    ax.text(3.4, 0.3, 'Ping-Pong', fontsize=7)
    ax.annotate('', xy=(5, 0.3), xytext=(4.5, 0.3),
                arrowprops=dict(arrowstyle='->', color='blue', lw=1.5))
    ax.text(5.2, 0.3, 'Result', fontsize=7)

    plt.tight_layout()
    buf = io.BytesIO()
    plt.savefig(buf, format='png', dpi=150, bbox_inches='tight')
    buf.seek(0)
    plt.close()
    return buf


def create_sequence_bandwidth():
    """Create bandwidth test sequence diagram between two agents"""
    fig, ax = plt.subplots(1, 1, figsize=(10, 6))
    ax.set_xlim(0, 10)
    ax.set_ylim(0, 8)
    ax.axis('off')

    ax.text(5, 7.7, 'Bandwidth Test (Agent-to-Agent)', ha='center', fontsize=14, fontweight='bold')

    # Actor labels
    actors = [('Controller', 1.5), ('Agent A\n(Sender)', 5), ('Agent B\n(Receiver)', 8.5)]
    for name, x in actors:
        ax.text(x, 7.2, name, ha='center', fontsize=10, fontweight='bold')
        ax.plot([x, x], [0.5, 6.8], 'k--', linewidth=1, alpha=0.5)

    y = 6.3
    step = 0.7

    # 1. Controller sends BANDWIDTH command
    ax.text(0.2, y, '1', fontsize=9, fontweight='bold', color='blue')
    ax.annotate('', xy=(5, y), xytext=(1.5, y),
                arrowprops=dict(arrowstyle='->', color='red', lw=1.5))
    ax.annotate('', xy=(8.5, y-0.1), xytext=(1.5, y-0.1),
                arrowprops=dict(arrowstyle='->', color='red', lw=1.5))
    ax.text(3.25, y+0.2, 'RDMA: BANDWIDTH cmd', fontsize=7, color='red')

    # Window transfer box
    y -= step
    ax.add_patch(FancyBboxPatch((4.2, y-2.0), 5, 2.2, boxstyle="round,pad=0.02",
                                 facecolor='#E6F3FF', edgecolor='blue', linewidth=2))
    ax.text(6.7, y, 'loop [window_size outstanding]', fontsize=8, fontweight='bold', color='blue')

    # 2. A sends window_size transfers
    y -= step * 0.7
    ax.text(0.2, y, '2', fontsize=9, fontweight='bold', color='blue')
    for i in range(3):
        ax.annotate('', xy=(8.5, y - i*0.25), xytext=(5, y - i*0.25),
                    arrowprops=dict(arrowstyle='->', color='purple', lw=1, alpha=0.7))
    ax.text(6.75, y+0.2, 'RDMA Write: data[0..window]', fontsize=7, color='purple')

    # 3. Wait for completion
    y -= step * 1.2
    ax.text(0.2, y, '3', fontsize=9, fontweight='bold', color='blue')
    ax.add_patch(FancyBboxPatch((4.3, y-0.15), 1.4, 0.35, facecolor='#98FB98'))
    ax.text(5, y, 'Wait ACKs', ha='center', fontsize=8)

    # 4. Next batch
    y -= step * 0.8
    ax.text(0.2, y, '4', fontsize=9, fontweight='bold', color='blue')
    ax.text(6.7, y, '... repeat for iterations ...', fontsize=8, style='italic')

    # 5. Calculate throughput
    y -= step
    ax.text(0.2, y, '5', fontsize=9, fontweight='bold', color='blue')
    ax.add_patch(FancyBboxPatch((4.3, y-0.15), 1.4, 0.35, facecolor='#FFD700'))
    ax.text(5, y, 'Calc BW', ha='center', fontsize=8)

    # 6. Write results
    y -= step
    ax.text(0.2, y, '6', fontsize=9, fontweight='bold', color='blue')
    ax.annotate('', xy=(1.5, y), xytext=(5, y),
                arrowprops=dict(arrowstyle='->', color='blue', lw=1.5))
    ax.text(3.25, y+0.18, 'RDMA: TestResult (bandwidth_mbps)', fontsize=7, color='blue')

    # Params note
    ax.text(0.3, 0.5, 'Params: msg_size (1K-4M), iterations, window_size', fontsize=8, color='gray')

    plt.tight_layout()
    buf = io.BytesIO()
    plt.savefig(buf, format='png', dpi=150, bbox_inches='tight')
    buf.seek(0)
    plt.close()
    return buf


def create_sequence_peer_discovery():
    """Create peer discovery sequence diagram"""
    fig, ax = plt.subplots(1, 1, figsize=(10, 5.5))
    ax.set_xlim(0, 10)
    ax.set_ylim(0, 7)
    ax.axis('off')

    ax.text(5, 6.7, 'Peer Discovery (After Rendezvous)', ha='center', fontsize=14, fontweight='bold')

    # Actor labels
    actors = [('Controller', 2), ('Agent 0', 5), ('Agent 1', 8)]
    for name, x in actors:
        ax.text(x, 6.2, name, ha='center', fontsize=10, fontweight='bold')
        ax.plot([x, x], [0.8, 6], 'k--', linewidth=1, alpha=0.5)

    y = 5.5
    step = 0.75

    # 1. Controller signals rendezvous complete
    ax.text(0.2, y, '1', fontsize=9, fontweight='bold', color='blue')
    ax.annotate('', xy=(5, y), xytext=(2, y),
                arrowprops=dict(arrowstyle='->', color='orange', lw=1.5))
    ax.annotate('', xy=(8, y), xytext=(2, y),
                arrowprops=dict(arrowstyle='->', color='orange', lw=1.5))
    ax.text(4.5, y+0.2, 'NIXL Notify: RENDEZVOUS_COMPLETE', fontsize=7, color='orange')

    # 2. Agent 0 reads controller buffer to get peer info
    y -= step
    ax.text(0.2, y, '2', fontsize=9, fontweight='bold', color='blue')
    ax.annotate('', xy=(5, y), xytext=(2, y),
                arrowprops=dict(arrowstyle='<-', color='green', lw=1.5))
    ax.text(3.5, y+0.2, 'RDMA Read: Agent slots', fontsize=7, color='green')

    # 3. Agent 0 loads peer metadata into NIXL
    y -= step
    ax.text(0.2, y, '3', fontsize=9, fontweight='bold', color='blue')
    ax.add_patch(FancyBboxPatch((4.3, y-0.15), 1.4, 0.35, facecolor='#B8D4E8'))
    ax.text(5, y, 'loadRemoteMD()', ha='center', fontsize=8)

    # 4. Agent 0 notifies controller it's ready
    y -= step
    ax.text(0.2, y, '4', fontsize=9, fontweight='bold', color='blue')
    ax.annotate('', xy=(2, y), xytext=(5, y),
                arrowprops=dict(arrowstyle='->', color='orange', lw=1.5))
    ax.text(3.5, y+0.2, 'NIXL Notify: PEER_DISCOVERY_COMPLETE', fontsize=7, color='orange')

    # 5. Agent 1 does the same
    y -= step
    ax.text(0.2, y, '5', fontsize=9, fontweight='bold', color='blue')
    ax.add_patch(FancyBboxPatch((7.3, y-0.15), 1.4, 0.35, facecolor='#B8D4E8'))
    ax.text(8, y, 'loadRemoteMD()', ha='center', fontsize=8)
    ax.annotate('', xy=(2, y-0.25), xytext=(8, y-0.25),
                arrowprops=dict(arrowstyle='->', color='orange', lw=1.5))

    # 6. Controller waits for all, then starts tests
    y -= step
    ax.text(0.2, y, '6', fontsize=9, fontweight='bold', color='blue')
    ax.add_patch(FancyBboxPatch((1.3, y-0.15), 1.4, 0.35, facecolor='#FFD700'))
    ax.text(2, y, 'Start tests', ha='center', fontsize=8)

    # Note about peer connectivity
    ax.text(0.3, 1.0, 'After discovery: agents can RDMA directly to each other\'s buffers', fontsize=8, color='gray')

    plt.tight_layout()
    buf = io.BytesIO()
    plt.savefig(buf, format='png', dpi=150, bbox_inches='tight')
    buf.seek(0)
    plt.close()
    return buf


def create_test_phases_diagram():
    """Create test phases overview diagram"""
    fig, ax = plt.subplots(1, 1, figsize=(10, 5))
    ax.set_xlim(0, 10)
    ax.set_ylim(0, 6)
    ax.axis('off')

    ax.text(5, 5.7, 'Incremental Test Phases & Graph Levels', ha='center', fontsize=14, fontweight='bold')

    # Phase 1
    phase1 = FancyBboxPatch((0.5, 3.5), 2.8, 1.8, boxstyle="round,pad=0.05",
                             facecolor='#90EE90', edgecolor='black', linewidth=2)
    ax.add_patch(phase1)
    ax.text(1.9, 5, 'PHASE 1', ha='center', fontsize=11, fontweight='bold')
    ax.text(1.9, 4.5, 'PAIRWISE_LATENCY', ha='center', fontsize=9)
    ax.text(1.9, 4.1, '64-byte ping-pong', ha='center', fontsize=8)
    ax.text(1.9, 3.7, '~Minutes', ha='center', fontsize=8, style='italic')

    # Arrow
    ax.annotate('', xy=(3.5, 4.4), xytext=(3.3, 4.4),
                arrowprops=dict(arrowstyle='->', color='black', lw=2))

    # Level 1 output
    level1 = FancyBboxPatch((3.6, 3.8), 2, 1.2, boxstyle="round,pad=0.03",
                             facecolor='#E6E6FA', edgecolor='purple', linewidth=1.5)
    ax.add_patch(level1)
    ax.text(4.6, 4.7, 'Level 1 Graph', ha='center', fontsize=9, fontweight='bold', color='purple')
    ax.text(4.6, 4.3, 'Topology tiers', ha='center', fontsize=8)
    ax.text(4.6, 4.0, 'Default BW', ha='center', fontsize=8)

    # Phase 2
    phase2 = FancyBboxPatch((0.5, 1.2), 2.8, 1.8, boxstyle="round,pad=0.05",
                             facecolor='#FFD700', edgecolor='black', linewidth=2)
    ax.add_patch(phase2)
    ax.text(1.9, 2.7, 'PHASE 2', ha='center', fontsize=11, fontweight='bold')
    ax.text(1.9, 2.3, 'BANDWIDTH SWEEP', ha='center', fontsize=9)
    ax.text(1.9, 1.95, '1K-4M bidirectional', ha='center', fontsize=8)
    ax.text(1.9, 1.6, '7 msg sizes per pair', ha='center', fontsize=7)
    ax.text(1.9, 1.35, '~30-60 min', ha='center', fontsize=8, style='italic')

    # Arrow
    ax.annotate('', xy=(3.5, 2.1), xytext=(3.3, 2.1),
                arrowprops=dict(arrowstyle='->', color='black', lw=2))

    # Level 2 output
    level2 = FancyBboxPatch((3.6, 1.5), 2, 1.2, boxstyle="round,pad=0.03",
                             facecolor='#FFB6C1', edgecolor='red', linewidth=1.5)
    ax.add_patch(level2)
    ax.text(4.6, 2.4, 'Level 2 Graph', ha='center', fontsize=9, fontweight='bold', color='red')
    ax.text(4.6, 2.0, '+ Measured BW', ha='center', fontsize=8)
    ax.text(4.6, 1.7, '+ Edge capacities', ha='center', fontsize=8)

    # Phase 2b (optional)
    phase2b = FancyBboxPatch((6, 1.2), 3.5, 1.8, boxstyle="round,pad=0.05",
                              facecolor='#FFA07A', edgecolor='black', linewidth=2)
    ax.add_patch(phase2b)
    ax.text(7.75, 2.7, 'PHASE 2b (Optional)', ha='center', fontsize=10, fontweight='bold')
    ax.text(7.75, 2.2, 'CONCURRENT_BANDWIDTH', ha='center', fontsize=9)
    ax.text(7.75, 1.85, 'Bottleneck detection', ha='center', fontsize=8)
    ax.text(7.75, 1.45, '~Hours', ha='center', fontsize=8, style='italic')

    # Level 3 output
    level3 = FancyBboxPatch((6.5, 3.8), 2.5, 1.2, boxstyle="round,pad=0.03",
                             facecolor='#87CEEB', edgecolor='blue', linewidth=1.5)
    ax.add_patch(level3)
    ax.text(7.75, 4.7, 'Level 3 Graph', ha='center', fontsize=9, fontweight='bold', color='blue')
    ax.text(7.75, 4.3, '+ Shared bottlenecks', ha='center', fontsize=8)
    ax.text(7.75, 4.0, '+ Contention analysis', ha='center', fontsize=8)

    ax.annotate('', xy=(7.75, 3.8), xytext=(7.75, 3),
                arrowprops=dict(arrowstyle='->', color='black', lw=2))

    # Time arrow at bottom
    ax.annotate('', xy=(9.5, 0.5), xytext=(0.5, 0.5),
                arrowprops=dict(arrowstyle='->', color='gray', lw=2))
    ax.text(5, 0.2, 'Increasing Detail & Time', ha='center', fontsize=9, color='gray')

    plt.tight_layout()
    buf = io.BytesIO()
    plt.savefig(buf, format='png', dpi=150, bbox_inches='tight')
    buf.seek(0)
    plt.close()
    return buf


def create_topology_algo_diagram():
    """Create topology algorithm flowchart"""
    fig, ax = plt.subplots(1, 1, figsize=(10, 7))
    ax.set_xlim(0, 10)
    ax.set_ylim(0, 8)
    ax.axis('off')

    ax.text(5, 7.7, 'Topology Inference Algorithm', ha='center', fontsize=14, fontweight='bold')

    # Input
    input_box = FancyBboxPatch((3.5, 6.8), 3, 0.7, boxstyle="round,pad=0.05",
                                facecolor='#90EE90', edgecolor='black', linewidth=2)
    ax.add_patch(input_box)
    ax.text(5, 7.15, 'Input: Latency Matrix L[N][N]', ha='center', fontsize=10, fontweight='bold')

    ax.annotate('', xy=(5, 6.3), xytext=(5, 6.8),
                arrowprops=dict(arrowstyle='->', color='black', lw=2))

    # Phase 1
    p1 = FancyBboxPatch((2.5, 5.4), 5, 0.8, boxstyle="round,pad=0.03",
                         facecolor='#B8D4E8', edgecolor='black', linewidth=1.5)
    ax.add_patch(p1)
    ax.text(5, 5.95, 'Phase 1: Hierarchical Clustering', ha='center', fontsize=10, fontweight='bold')
    ax.text(5, 5.6, 'Agglomerative clustering by latency distance', ha='center', fontsize=8)

    ax.annotate('', xy=(5, 5), xytext=(5, 5.4),
                arrowprops=dict(arrowstyle='->', color='black', lw=2))

    # Phase 2
    p2 = FancyBboxPatch((2.5, 4.1), 5, 0.8, boxstyle="round,pad=0.03",
                         facecolor='#FFE4B5', edgecolor='black', linewidth=1.5)
    ax.add_patch(p2)
    ax.text(5, 4.65, 'Phase 2: Hidden Node Inference', ha='center', fontsize=10, fontweight='bold')
    ax.text(5, 4.3, 'Infer switches from cluster structure', ha='center', fontsize=8)

    ax.annotate('', xy=(5, 3.7), xytext=(5, 4.1),
                arrowprops=dict(arrowstyle='->', color='black', lw=2))

    # Phase 3
    p3 = FancyBboxPatch((2.5, 2.8), 5, 0.8, boxstyle="round,pad=0.03",
                         facecolor='#DDA0DD', edgecolor='black', linewidth=1.5)
    ax.add_patch(p3)
    ax.text(5, 3.35, 'Phase 3: Edge Construction', ha='center', fontsize=10, fontweight='bold')
    ax.text(5, 3.0, 'Connect nodes, estimate capacities', ha='center', fontsize=8)

    ax.annotate('', xy=(5, 2.4), xytext=(5, 2.8),
                arrowprops=dict(arrowstyle='->', color='black', lw=2))

    # Phase 4
    p4 = FancyBboxPatch((2.5, 1.5), 5, 0.8, boxstyle="round,pad=0.03",
                         facecolor='#FFA07A', edgecolor='black', linewidth=1.5)
    ax.add_patch(p4)
    ax.text(5, 2.05, 'Phase 4: Bottleneck Detection (Optional)', ha='center', fontsize=10, fontweight='bold')
    ax.text(5, 1.7, 'Concurrent BW correlation analysis', ha='center', fontsize=8)

    ax.annotate('', xy=(5, 1.1), xytext=(5, 1.5),
                arrowprops=dict(arrowstyle='->', color='black', lw=2))

    # Output
    output_box = FancyBboxPatch((3, 0.3), 4, 0.7, boxstyle="round,pad=0.05",
                                 facecolor='#98FB98', edgecolor='black', linewidth=2)
    ax.add_patch(output_box)
    ax.text(5, 0.65, 'Output: G(V, HV, E)', ha='center', fontsize=10, fontweight='bold')

    # Tier thresholds
    ax.text(8.2, 5.6, 'Tier Thresholds:', fontsize=8, fontweight='bold')
    ax.text(8.2, 5.3, 'T0: <5us (NVLink)', fontsize=7)
    ax.text(8.2, 5.05, 'T1: <15us (ToR)', fontsize=7)
    ax.text(8.2, 4.8, 'T2: <50us (Spine)', fontsize=7)

    plt.tight_layout()
    buf = io.BytesIO()
    plt.savefig(buf, format='png', dpi=150, bbox_inches='tight')
    buf.seek(0)
    plt.close()
    return buf


def create_topology_example():
    """Create topology example: input matrix -> output graph"""
    fig, axes = plt.subplots(1, 2, figsize=(11, 5))

    # Left: Input latency matrix
    ax1 = axes[0]
    matrix = np.array([
        [0, 2, 2, 15, 15, 50],
        [2, 0, 2, 15, 15, 50],
        [2, 2, 0, 15, 15, 50],
        [15, 15, 15, 0, 3, 50],
        [15, 15, 15, 3, 0, 50],
        [50, 50, 50, 50, 50, 0]
    ])

    im = ax1.imshow(matrix, cmap='YlOrRd', aspect='auto')
    ax1.set_xticks(range(6))
    ax1.set_yticks(range(6))
    ax1.set_xticklabels(['N0', 'N1', 'N2', 'N3', 'N4', 'N5'])
    ax1.set_yticklabels(['N0', 'N1', 'N2', 'N3', 'N4', 'N5'])
    ax1.set_title('Input: Latency Matrix (us)', fontsize=11, fontweight='bold')

    # Add values to cells
    for i in range(6):
        for j in range(6):
            ax1.text(j, i, str(matrix[i, j]), ha='center', va='center', fontsize=9)

    plt.colorbar(im, ax=ax1, shrink=0.8)

    # Right: Output topology graph
    ax2 = axes[1]
    ax2.set_xlim(0, 10)
    ax2.set_ylim(0, 8)
    ax2.axis('off')
    ax2.set_title('Output: Inferred Topology', fontsize=11, fontweight='bold')

    # Spine (hidden node)
    spine = FancyBboxPatch((4, 6.5), 2, 0.8, boxstyle="round,pad=0.05",
                            facecolor='#FFB6C1', edgecolor='black', linewidth=1.5, linestyle='--')
    ax2.add_patch(spine)
    ax2.text(5, 6.9, 'Spine', ha='center', fontsize=9, fontweight='bold')

    # ToR switches (hidden nodes)
    tor1 = FancyBboxPatch((1, 4.5), 1.5, 0.7, boxstyle="round,pad=0.03",
                           facecolor='#DDA0DD', edgecolor='black', linewidth=1.5, linestyle='--')
    ax2.add_patch(tor1)
    ax2.text(1.75, 4.85, 'ToR-0', ha='center', fontsize=8, fontweight='bold')

    tor2 = FancyBboxPatch((4.25, 4.5), 1.5, 0.7, boxstyle="round,pad=0.03",
                           facecolor='#DDA0DD', edgecolor='black', linewidth=1.5, linestyle='--')
    ax2.add_patch(tor2)
    ax2.text(5, 4.85, 'ToR-1', ha='center', fontsize=8, fontweight='bold')

    # Physical nodes - Cluster 1
    for i, x in enumerate([0.5, 1.75, 3]):
        node = FancyBboxPatch((x, 2.5), 1, 0.7, boxstyle="round,pad=0.03",
                               facecolor='#90EE90', edgecolor='black', linewidth=1.5)
        ax2.add_patch(node)
        ax2.text(x+0.5, 2.85, f'N{i}', ha='center', fontsize=9, fontweight='bold')
        # Edge to ToR
        ax2.plot([x+0.5, 1.75], [3.2, 4.5], 'b-', linewidth=1.5)

    # Physical nodes - Cluster 2
    for i, x in enumerate([4, 5.5]):
        node = FancyBboxPatch((x, 2.5), 1, 0.7, boxstyle="round,pad=0.03",
                               facecolor='#90EE90', edgecolor='black', linewidth=1.5)
        ax2.add_patch(node)
        ax2.text(x+0.5, 2.85, f'N{i+3}', ha='center', fontsize=9, fontweight='bold')
        ax2.plot([x+0.5, 5], [3.2, 4.5], 'b-', linewidth=1.5)

    # Isolated node
    node5 = FancyBboxPatch((7.5, 2.5), 1, 0.7, boxstyle="round,pad=0.03",
                            facecolor='#90EE90', edgecolor='black', linewidth=1.5)
    ax2.add_patch(node5)
    ax2.text(8, 2.85, 'N5', ha='center', fontsize=9, fontweight='bold')

    # Edges to spine
    ax2.plot([1.75, 5], [5.2, 6.5], 'r-', linewidth=1.5)
    ax2.plot([5, 5], [5.2, 6.5], 'r-', linewidth=1.5)
    ax2.plot([8, 5.5], [3.2, 6.5], 'r-', linewidth=1.5)

    # Legend
    ax2.add_patch(FancyBboxPatch((0.2, 0.5), 0.4, 0.3, facecolor='#90EE90'))
    ax2.text(0.8, 0.65, 'Physical', fontsize=7)
    ax2.add_patch(FancyBboxPatch((2, 0.5), 0.4, 0.3, facecolor='#DDA0DD', linestyle='--'))
    ax2.text(2.6, 0.65, 'Inferred', fontsize=7)
    ax2.text(4, 0.65, 'Tier0:<5us  Tier1:<15us  Tier2:<50us', fontsize=7)

    plt.tight_layout()
    buf = io.BytesIO()
    plt.savefig(buf, format='png', dpi=150, bbox_inches='tight')
    buf.seek(0)
    plt.close()
    return buf


def create_test_harness_diagram():
    """Create test harness deployment diagram with tc network emulation"""
    fig, ax = plt.subplots(1, 1, figsize=(11, 7))
    ax.set_xlim(0, 11)
    ax.set_ylim(0, 8)
    ax.axis('off')

    ax.text(5.5, 7.7, 'Test Harness: TC Network Emulation', ha='center', fontsize=14, fontweight='bold')

    # Docker environment
    env = FancyBboxPatch((0.3, 1.8), 10.4, 5.6, boxstyle="round,pad=0.02",
                          facecolor='#F5F5F5', edgecolor='gray', linewidth=2, linestyle='--')
    ax.add_patch(env)
    ax.text(5.5, 7.2, 'Docker Network (harness-net: 172.30.0.0/16)', ha='center', fontsize=10)

    # Harness (external)
    harness = FancyBboxPatch((0.3, 0.3), 2.5, 1.2, boxstyle="round,pad=0.05",
                              facecolor='#9B59B6', edgecolor='black', linewidth=2)
    ax.add_patch(harness)
    ax.text(1.55, 1.05, 'Harness', ha='center', fontsize=10, fontweight='bold', color='white')
    ax.text(1.55, 0.7, 'tc_manager', ha='center', fontsize=8, color='white')
    ax.text(1.55, 0.45, 'result_collector', ha='center', fontsize=7, color='white')

    # Controller container
    ctrl = FancyBboxPatch((4, 5.8), 3, 1.2, boxstyle="round,pad=0.05",
                           facecolor='#4A90D9', edgecolor='black', linewidth=2)
    ax.add_patch(ctrl)
    ax.text(5.5, 6.55, 'Controller', ha='center', fontsize=10, fontweight='bold', color='white')
    ax.text(5.5, 6.2, '172.30.0.2', ha='center', fontsize=8, color='white')
    ax.text(5.5, 5.9, '/output mounted', ha='center', fontsize=7, color='white')

    # Rack 0 box
    rack0 = FancyBboxPatch((0.5, 2.2), 4.5, 3.2, boxstyle="round,pad=0.02",
                            facecolor='#E8F5E9', edgecolor='#4CAF50', linewidth=2)
    ax.add_patch(rack0)
    ax.text(2.75, 5.2, 'Tier 0 (Rack 0)', ha='center', fontsize=9, fontweight='bold', color='#2E7D32')
    ax.text(2.75, 4.9, 'intra_tier: 100us', ha='center', fontsize=8, color='#2E7D32')

    # Rack 0 agents
    for i, x in enumerate([0.7, 2.5]):
        agent = FancyBboxPatch((x, 2.5), 1.6, 1.8, boxstyle="round,pad=0.05",
                                facecolor='#50C878', edgecolor='black', linewidth=1.5)
        ax.add_patch(agent)
        ax.text(x+0.8, 4.0, f'Agent {i}', ha='center', fontsize=9, fontweight='bold', color='white')
        ax.text(x+0.8, 3.6, f'172.30.0.{10+i}', ha='center', fontsize=7, color='white')
        ax.text(x+0.8, 3.2, 'tc qdisc:', ha='center', fontsize=7, color='white')
        ax.text(x+0.8, 2.85, 'netem delay', ha='center', fontsize=6, color='white')

    # Rack 1 box
    rack1 = FancyBboxPatch((5.5, 2.2), 4.5, 3.2, boxstyle="round,pad=0.02",
                            facecolor='#FFF3E0', edgecolor='#FF9800', linewidth=2)
    ax.add_patch(rack1)
    ax.text(7.75, 5.2, 'Tier 1 (Rack 1)', ha='center', fontsize=9, fontweight='bold', color='#E65100')
    ax.text(7.75, 4.9, 'intra_tier: 100us', ha='center', fontsize=8, color='#E65100')

    # Rack 1 agents
    for i, x in enumerate([5.7, 7.5]):
        agent = FancyBboxPatch((x, 2.5), 1.6, 1.8, boxstyle="round,pad=0.05",
                                facecolor='#50C878', edgecolor='black', linewidth=1.5)
        ax.add_patch(agent)
        ax.text(x+0.8, 4.0, f'Agent {i+2}', ha='center', fontsize=9, fontweight='bold', color='white')
        ax.text(x+0.8, 3.6, f'172.30.0.{12+i}', ha='center', fontsize=7, color='white')
        ax.text(x+0.8, 3.2, 'tc qdisc:', ha='center', fontsize=7, color='white')
        ax.text(x+0.8, 2.85, 'netem delay', ha='center', fontsize=6, color='white')

    # Inter-tier arrow
    ax.annotate('', xy=(5.3, 3.5), xytext=(4.7, 3.5),
                arrowprops=dict(arrowstyle='<->', color='red', lw=2))
    ax.text(5, 3.9, 'inter_tier:', ha='center', fontsize=7, color='red')
    ax.text(5, 3.65, '5000us', ha='center', fontsize=7, color='red')

    # Harness to containers arrows
    ax.annotate('', xy=(4, 6.4), xytext=(2.8, 1.2),
                arrowprops=dict(arrowstyle='->', color='purple', lw=1.5, connectionstyle='arc3,rad=0.2'))
    ax.text(3.2, 1.5, 'docker run', fontsize=7, color='purple')

    ax.annotate('', xy=(1.5, 2.2), xytext=(1.5, 1.5),
                arrowprops=dict(arrowstyle='->', color='orange', lw=1.5))
    ax.text(1.8, 1.85, 'docker exec tc', fontsize=7, color='orange')

    # Legend
    ax.text(7, 0.9, 'TC Rules per Agent:', fontsize=8, fontweight='bold')
    ax.text(7, 0.6, 'HTB root → netem classes', fontsize=7)
    ax.text(7, 0.35, 'Filter by dest IP → delay class', fontsize=7)

    plt.tight_layout()
    buf = io.BytesIO()
    plt.savefig(buf, format='png', dpi=150, bbox_inches='tight')
    buf.seek(0)
    plt.close()
    return buf


def create_tier_threshold_diagram():
    """Create diagram showing tier threshold calculation from tc params"""
    fig, ax = plt.subplots(1, 1, figsize=(10, 5))
    ax.set_xlim(0, 10)
    ax.set_ylim(0, 6)
    ax.axis('off')

    ax.text(5, 5.7, 'Tier Threshold Generation from TC Parameters', ha='center', fontsize=14, fontweight='bold')

    # Input: cluster_config.json
    input_box = FancyBboxPatch((0.3, 3.5), 3.5, 1.8, boxstyle="round,pad=0.05",
                                facecolor='#E3F2FD', edgecolor='#1976D2', linewidth=2)
    ax.add_patch(input_box)
    ax.text(2.05, 5.0, 'cluster_config.json', ha='center', fontsize=10, fontweight='bold', color='#1565C0')
    ax.text(2.05, 4.6, 'network_shaping:', ha='center', fontsize=8)
    ax.text(2.05, 4.25, 'intra_tier: 100us', ha='center', fontsize=8, color='#4CAF50')
    ax.text(2.05, 3.9, 'inter_tier: 5000us', ha='center', fontsize=8, color='#F44336')

    # Arrow
    ax.annotate('', xy=(4.2, 4.4), xytext=(3.8, 4.4),
                arrowprops=dict(arrowstyle='->', color='black', lw=2))

    # Formula box
    formula_box = FancyBboxPatch((4.3, 3.5), 2.8, 1.8, boxstyle="round,pad=0.05",
                                  facecolor='#FFF9C4', edgecolor='#FBC02D', linewidth=2)
    ax.add_patch(formula_box)
    ax.text(5.7, 5.0, 'Threshold Calc', ha='center', fontsize=10, fontweight='bold', color='#F57F17')
    ax.text(5.7, 4.5, 'geometric mean:', ha='center', fontsize=8)
    ax.text(5.7, 4.1, '√(100 × 5000)', ha='center', fontsize=9, fontweight='bold')
    ax.text(5.7, 3.7, '= 707 us', ha='center', fontsize=9, fontweight='bold', color='#E65100')

    # Arrow
    ax.annotate('', xy=(7.5, 4.4), xytext=(7.1, 4.4),
                arrowprops=dict(arrowstyle='->', color='black', lw=2))

    # Output: tier_config.json
    output_box = FancyBboxPatch((7.6, 3.5), 2.1, 1.8, boxstyle="round,pad=0.05",
                                 facecolor='#E8F5E9', edgecolor='#4CAF50', linewidth=2)
    ax.add_patch(output_box)
    ax.text(8.65, 5.0, 'tier_config.json', ha='center', fontsize=10, fontweight='bold', color='#2E7D32')
    ax.text(8.65, 4.5, 'thresholds_ns:', ha='center', fontsize=8)
    ax.text(8.65, 4.1, '[707000]', ha='center', fontsize=9, fontweight='bold')

    # Dendrogram representation
    ax.text(5, 3.0, 'Dendrogram Cut Point', ha='center', fontsize=10, fontweight='bold')

    # Simple dendrogram
    # Vertical lines for agents
    for i, x in enumerate([1.5, 2.5, 3.5, 6.5, 7.5, 8.5]):
        ax.plot([x, x], [0.5, 1.2], 'k-', linewidth=1.5)
        ax.text(x, 0.3, f'A{i}', ha='center', fontsize=8)

    # Intra-tier merges (low)
    ax.plot([1.5, 2.5], [1.2, 1.2], 'g-', linewidth=2)
    ax.plot([2, 2], [1.2, 1.5], 'g-', linewidth=2)
    ax.plot([2, 3.5], [1.5, 1.5], 'g-', linewidth=2)
    ax.plot([2.75, 2.75], [1.5, 1.8], 'g-', linewidth=2)

    ax.plot([6.5, 7.5], [1.2, 1.2], 'g-', linewidth=2)
    ax.plot([7, 7], [1.2, 1.5], 'g-', linewidth=2)
    ax.plot([7, 8.5], [1.5, 1.5], 'g-', linewidth=2)
    ax.plot([7.75, 7.75], [1.5, 1.8], 'g-', linewidth=2)

    # Inter-tier merge (high)
    ax.plot([2.75, 7.75], [2.5, 2.5], 'r-', linewidth=2)
    ax.plot([2.75, 2.75], [1.8, 2.5], 'b-', linewidth=2)
    ax.plot([7.75, 7.75], [1.8, 2.5], 'b-', linewidth=2)

    # Threshold line
    ax.plot([0.5, 9.5], [2.1, 2.1], 'r--', linewidth=2, alpha=0.7)
    ax.text(9.7, 2.1, '707us', fontsize=8, color='red', va='center')

    # Labels
    ax.text(0.5, 1.2, '100us', fontsize=7, color='green')
    ax.text(0.5, 2.5, '5000us', fontsize=7, color='red')

    plt.tight_layout()
    buf = io.BytesIO()
    plt.savefig(buf, format='png', dpi=150, bbox_inches='tight')
    buf.seek(0)
    plt.close()
    return buf


def create_result_collection_diagram():
    """Create diagram showing result collection flow from containers"""
    fig, ax = plt.subplots(1, 1, figsize=(10, 5))
    ax.set_xlim(0, 10)
    ax.set_ylim(0, 6)
    ax.axis('off')

    ax.text(5, 5.7, 'Result Collection Flow', ha='center', fontsize=14, fontweight='bold')

    # Controller container
    ctrl = FancyBboxPatch((0.5, 3.5), 2.5, 1.8, boxstyle="round,pad=0.05",
                           facecolor='#4A90D9', edgecolor='black', linewidth=2)
    ax.add_patch(ctrl)
    ax.text(1.75, 5.0, 'Controller', ha='center', fontsize=10, fontweight='bold', color='white')
    ax.text(1.75, 4.5, 'Writes to:', ha='center', fontsize=8, color='white')
    ax.text(1.75, 4.1, '/output/*.csv', ha='center', fontsize=8, color='white')
    ax.text(1.75, 3.7, '/output/*.json', ha='center', fontsize=7, color='white')

    # Volume mount arrow
    ax.annotate('', xy=(3.3, 4.4), xytext=(3.0, 4.4),
                arrowprops=dict(arrowstyle='->', color='purple', lw=2))
    ax.text(3.15, 4.7, 'volume', fontsize=7, color='purple')
    ax.text(3.15, 4.15, 'mount', fontsize=7, color='purple')

    # Host output directory
    host_out = FancyBboxPatch((3.4, 3.5), 2.3, 1.8, boxstyle="round,pad=0.05",
                               facecolor='#FFE0B2', edgecolor='#FF9800', linewidth=2)
    ax.add_patch(host_out)
    ax.text(4.55, 5.0, 'Host: ./output/', ha='center', fontsize=10, fontweight='bold', color='#E65100')
    ax.text(4.55, 4.5, 'latency_matrix.csv', ha='center', fontsize=8)
    ax.text(4.55, 4.1, 'bandwidth_*.csv', ha='center', fontsize=8)
    ax.text(4.55, 3.7, '.tc_ready (signal)', ha='center', fontsize=7, color='gray')

    # Harness collect arrow
    ax.annotate('', xy=(6.0, 4.4), xytext=(5.7, 4.4),
                arrowprops=dict(arrowstyle='->', color='green', lw=2))
    ax.text(5.85, 4.7, 'harness', fontsize=7, color='green')
    ax.text(5.85, 4.15, 'collect', fontsize=7, color='green')

    # Timestamped results
    results = FancyBboxPatch((6.1, 3.2), 3.5, 2.4, boxstyle="round,pad=0.05",
                              facecolor='#E8F5E9', edgecolor='#4CAF50', linewidth=2)
    ax.add_patch(results)
    ax.text(7.85, 5.3, './output/2024-12-17_14-30/', ha='center', fontsize=9, fontweight='bold', color='#2E7D32')
    ax.text(7.85, 4.8, 'latency_matrix.csv', ha='center', fontsize=8)
    ax.text(7.85, 4.45, 'bandwidth_matrix.csv', ha='center', fontsize=8)
    ax.text(7.85, 4.1, 'bandwidth_detailed.csv', ha='center', fontsize=8)
    ax.text(7.85, 3.75, 'tier_config.json (generated)', ha='center', fontsize=8, color='#1976D2')
    ax.text(7.85, 3.4, 'topology.svg (generated)', ha='center', fontsize=8, color='#1976D2')

    # Flow description
    ax.text(5, 2.7, 'Collection Steps:', ha='center', fontsize=10, fontweight='bold')

    steps = [
        '1. Wait for controller container to exit',
        '2. Create timestamped results directory',
        '3. Copy CSV/JSON files from ./output/',
        '4. Generate tier_config.json from tc params',
        '5. Run topology_viz → topology.svg'
    ]

    for i, step in enumerate(steps):
        ax.text(1, 2.2 - i*0.4, step, fontsize=8)

    plt.tight_layout()
    buf = io.BytesIO()
    plt.savefig(buf, format='png', dpi=150, bbox_inches='tight')
    buf.seek(0)
    plt.close()
    return buf


def create_error_handling_diagram():
    """Create error handling overview"""
    fig, ax = plt.subplots(1, 1, figsize=(9, 4))
    ax.set_xlim(0, 10)
    ax.set_ylim(0, 5)
    ax.axis('off')

    ax.text(5, 4.7, 'Error Handling & Recovery', ha='center', fontsize=14, fontweight='bold')

    # Heartbeat
    hb = FancyBboxPatch((0.3, 2.8), 3, 1.5, boxstyle="round,pad=0.05",
                         facecolor='#90EE90', edgecolor='black', linewidth=1.5)
    ax.add_patch(hb)
    ax.text(1.8, 4, 'Heartbeat', ha='center', fontsize=10, fontweight='bold')
    ax.text(1.8, 3.6, 'Agent: 5s interval', ha='center', fontsize=8)
    ax.text(1.8, 3.25, 'Timeout: 30s', ha='center', fontsize=8)
    ax.text(1.8, 2.9, 'Health: OK/SLOW/DEAD', ha='center', fontsize=8)

    # Graceful degradation
    gd = FancyBboxPatch((3.5, 2.8), 3, 1.5, boxstyle="round,pad=0.05",
                         facecolor='#FFD700', edgecolor='black', linewidth=1.5)
    ax.add_patch(gd)
    ax.text(5, 4, 'Graceful Degradation', ha='center', fontsize=10, fontweight='bold')
    ax.text(5, 3.6, 'Skip dead agents', ha='center', fontsize=8)
    ax.text(5, 3.25, 'Retry failed pairs', ha='center', fontsize=8)
    ax.text(5, 2.9, 'Accept partial results', ha='center', fontsize=8)

    # NIXL retry
    nr = FancyBboxPatch((6.7, 2.8), 3, 1.5, boxstyle="round,pad=0.05",
                         facecolor='#87CEEB', edgecolor='black', linewidth=1.5)
    ax.add_patch(nr)
    ax.text(8.2, 4, 'NIXL Retry', ha='center', fontsize=10, fontweight='bold')
    ax.text(8.2, 3.6, 'Exponential backoff', ha='center', fontsize=8)
    ax.text(8.2, 3.25, 'Max 3 retries', ha='center', fontsize=8)
    ax.text(8.2, 2.9, 'Transient vs Permanent', ha='center', fontsize=8)

    # Failure table
    table_data = [
        ['Failure', 'Detection', 'Recovery'],
        ['Agent crash', 'Heartbeat timeout', 'Skip pair, continue'],
        ['Network issue', 'NIXL error', 'Retry with backoff'],
        ['Test timeout', 'Deadline exceeded', 'Mark failed, next pair']
    ]

    y_start = 2.3
    for i, row in enumerate(table_data):
        for j, cell in enumerate(row):
            x = 0.5 + j * 3.2
            bg_color = '#E8E8E8' if i == 0 else 'white'
            fontweight = 'bold' if i == 0 else 'normal'
            ax.add_patch(FancyBboxPatch((x, y_start - i*0.45), 3, 0.4,
                                         facecolor=bg_color, edgecolor='gray', linewidth=0.5))
            ax.text(x + 1.5, y_start - i*0.45 + 0.2, cell, ha='center', va='center',
                    fontsize=7, fontweight=fontweight)

    plt.tight_layout()
    buf = io.BytesIO()
    plt.savefig(buf, format='png', dpi=150, bbox_inches='tight')
    buf.seek(0)
    plt.close()
    return buf


def build_pdf():
    """Build the complete PDF document"""
    doc = SimpleDocTemplate(
        "/home/rarun/nixl-topo-disc/docs/NIXL_Topology_Discovery_Design.pdf",
        pagesize=letter,
        rightMargin=0.5*inch,
        leftMargin=0.5*inch,
        topMargin=0.5*inch,
        bottomMargin=0.5*inch
    )

    styles = getSampleStyleSheet()
    styles.add(ParagraphStyle(
        name='Title2',
        parent=styles['Title'],
        fontSize=20,
        spaceAfter=20
    ))
    styles.add(ParagraphStyle(
        name='Section',
        parent=styles['Heading1'],
        fontSize=14,
        spaceBefore=12,
        spaceAfter=8,
        textColor=colors.darkblue
    ))
    styles.add(ParagraphStyle(
        name='Body',
        parent=styles['Normal'],
        fontSize=10,
        spaceBefore=4,
        spaceAfter=8
    ))

    story = []

    # Title page
    story.append(Spacer(1, 1*inch))
    story.append(Paragraph("NIXL Cluster Topology Discovery", styles['Title2']))
    story.append(Paragraph("System Design Document", styles['Heading2']))
    story.append(Spacer(1, 0.3*inch))
    story.append(Paragraph("A distributed agent system for measuring memory transfer performance and inferring physical interconnect topology using NVIDIA NIXL framework.", styles['Body']))
    story.append(Spacer(1, 0.5*inch))

    # Architecture diagram
    img_buf = create_component_diagram()
    story.append(KeepTogether([
        Paragraph("1. System Architecture", styles['Section']),
        Paragraph("Agents run as containers spawned by the Controller via orchestrator (Docker/K8s). All communication uses NIXL RDMA transfers through a shared controller buffer - no TCP sockets. Each agent registers its NIXL endpoint during bootstrap and receives peer metadata via the controller buffer.", styles['Body']),
        Image(img_buf, width=7*inch, height=4.2*inch),
    ]))

    story.append(PageBreak())

    # Buffer layout
    img_buf = create_buffer_layout_diagram()
    story.append(KeepTogether([
        Paragraph("2. Controller Buffer Layout", styles['Section']),
        Paragraph("Single NIXL-registered buffer containing all shared state: agent metadata slots for endpoint blobs, test command region for orchestration, and results region for data collection. NIXL notifications (via genNotif/getNotifs API) are used for async coordination. Agents compute their slot offsets by reading the header.", styles['Body']),
        Image(img_buf, width=6.5*inch, height=3.6*inch),
    ]))
    story.append(Spacer(1, 0.2*inch))

    # Agent buffer layout
    img_buf = create_agent_buffer_layout_diagram()
    story.append(KeepTogether([
        Paragraph("2b. Agent Buffer Layout", styles['Section']),
        Paragraph("Each agent allocates a 256MB NIXL-registered buffer. Transfer slots receive data from peer agents during bandwidth tests. Command inbox receives test commands from controller. Mailbox is used for ping-pong latency measurements.", styles['Body']),
        Image(img_buf, width=6.5*inch, height=2.6*inch),
    ]))

    # Bootstrap sequence
    img_buf = create_sequence_diagram_bootstrap()
    story.append(KeepTogether([
        Paragraph("3. Bootstrap & Rendezvous Sequence", styles['Section']),
        Paragraph("Controller spawns agent containers with NIXL metadata via environment variables (CTRL_ENDPOINT, CTRL_BUFFER, AGENT_ID). Agents decode this metadata, establish NIXL connections, and write their own endpoint info. Controller notifies all agents when rendezvous is complete.", styles['Body']),
        Image(img_buf, width=7*inch, height=4.9*inch),
    ]))

    story.append(PageBreak())

    # Peer discovery sequence
    img_buf = create_sequence_peer_discovery()
    story.append(KeepTogether([
        Paragraph("3b. Peer Discovery Sequence", styles['Section']),
        Paragraph("After rendezvous completes, agents read peer metadata from the controller buffer via RDMA, load remote endpoint descriptors into NIXL using loadRemoteMD(), and signal readiness. This enables direct agent-to-agent RDMA communication for performance tests.", styles['Body']),
        Image(img_buf, width=7*inch, height=3.8*inch),
    ]))
    story.append(Spacer(1, 0.2*inch))

    # Test sequence
    img_buf = create_sequence_diagram_test()
    story.append(KeepTogether([
        Paragraph("4. Test Execution Protocol", styles['Section']),
        Paragraph("Controller writes test commands to agent command inboxes via RDMA and sends NIXL notifications to wake agents. Agents poll for commands, execute tests, and write results back to the controller's result region. The NIXL notification API (genNotif/getNotifs) provides the async coordination layer.", styles['Body']),
        Image(img_buf, width=7*inch, height=4.2*inch),
    ]))

    story.append(PageBreak())

    # Ping-pong latency detail
    img_buf = create_sequence_ping_pong()
    story.append(KeepTogether([
        Paragraph("4a. Ping-Pong Latency Test Detail", styles['Section']),
        Paragraph("The initiator agent sends a small message to the responder's mailbox via RDMA write, then polls its own mailbox waiting for a response. The responder polls, detects the incoming message (via sequence number increment), and responds. Round-trip time is measured over thousands of iterations to compute average latency.", styles['Body']),
        Image(img_buf, width=7*inch, height=4.2*inch),
    ]))
    story.append(Spacer(1, 0.2*inch))

    # Bandwidth test detail
    img_buf = create_sequence_bandwidth()
    story.append(KeepTogether([
        Paragraph("4b. Bandwidth Test Detail", styles['Section']),
        Paragraph("The sender issues a window of concurrent RDMA writes to the receiver's transfer slots, waits for all completions, then repeats for the configured iteration count. Throughput is calculated from total bytes transferred divided by elapsed time. Window size enables pipelining to saturate the link.", styles['Body']),
        Image(img_buf, width=7*inch, height=4.2*inch),
    ]))
    story.append(Spacer(1, 0.2*inch))

    # Test phases
    img_buf = create_test_phases_diagram()
    story.append(KeepTogether([
        Paragraph("5. Incremental Test Phases", styles['Section']),
        Paragraph("Phase 1 (PAIRWISE_LATENCY) runs quickly with 64-byte ping-pong to produce Level 1 topology with tiers. Phase 2 (BANDWIDTH) sweeps 7 message sizes (1K, 4K, 16K, 64K, 256K, 1M, 4M bytes) bidirectionally per pair, outputting latency_matrix.csv, bandwidth_matrix.csv, and bandwidth_detailed.csv. Optional Phase 2b detects shared bottlenecks via concurrent transfer correlation.", styles['Body']),
        Image(img_buf, width=7*inch, height=3.5*inch),
    ]))

    story.append(PageBreak())

    # Topology algorithm
    img_buf = create_topology_algo_diagram()
    story.append(KeepTogether([
        Paragraph("6. Topology Inference Algorithm", styles['Section']),
        Paragraph("Four-phase algorithm: (1) Hierarchical clustering groups nodes by latency similarity, (2) Hidden node inference creates switch nodes for clusters, (3) Edge construction connects physical to hidden nodes with capacity estimates, (4) Optional bottleneck detection identifies shared links.", styles['Body']),
        Image(img_buf, width=6.5*inch, height=4.5*inch),
    ]))
    story.append(Spacer(1, 0.2*inch))

    # Topology example
    img_buf = create_topology_example()
    story.append(KeepTogether([
        Paragraph("7. Topology Example: Input to Output", styles['Section']),
        Paragraph("Example: 6-node cluster with two tight clusters (N0-N2 at 2us, N3-N4 at 3us) and one isolated node (N5 at 50us). Algorithm infers ToR switches for each cluster and spine switch connecting them, producing hierarchical graph.", styles['Body']),
        Image(img_buf, width=7.5*inch, height=3.4*inch),
    ]))

    story.append(PageBreak())

    # Test harness
    img_buf = create_test_harness_diagram()
    story.append(KeepTogether([
        Paragraph("8. Test Harness: TC Network Emulation", styles['Section']),
        Paragraph("The test harness creates simulated cluster topologies using Linux tc (traffic control) with netem (network emulation). This enables testing topology discovery algorithms without physical multi-rack infrastructure. The harness manages container lifecycle, applies tc rules to inject latency between tiers, and collects results.", styles['Body']),
        Image(img_buf, width=7.5*inch, height=4.8*inch),
    ]))

    story.append(Paragraph("<b>TC Rule Structure:</b> Each agent container gets HTB (Hierarchical Token Bucket) qdisc with netem classes. Traffic is classified by destination IP: intra-tier traffic (same rack) gets low delay (e.g., 100us), inter-tier traffic (cross-rack) gets high delay (e.g., 5000us). Filters route packets to appropriate delay class based on destination.", styles['Body']))

    story.append(Paragraph("<b>Configuration Example (two_rack.json):</b>", styles['Body']))
    config_example = """
    {
      "network_shaping": {
        "tiers": [
          { "name": "rack-0", "agents": [0,1], "intra_tier_latency_us": 100 },
          { "name": "rack-1", "agents": [2,3], "intra_tier_latency_us": 100 }
        ],
        "inter_tier": { "latency_us": 5000, "jitter_us": 500 }
      }
    }
    """
    story.append(Paragraph(f"<font face='Courier' size='8'>{config_example}</font>", styles['Body']))

    story.append(PageBreak())

    # Tier threshold generation
    img_buf = create_tier_threshold_diagram()
    story.append(KeepTogether([
        Paragraph("8b. Tier Threshold Generation", styles['Section']),
        Paragraph("The harness automatically generates tier_config.json for the topology discovery algorithm using the geometric mean of tc latency parameters. This provides optimal log-scale separation for hierarchical clustering - the threshold falls naturally between intra-tier and inter-tier latencies.", styles['Body']),
        Image(img_buf, width=7*inch, height=3.5*inch),
    ]))

    story.append(Paragraph("<b>Formula:</b> threshold = √(max_intra_tier × inter_tier). For intra=100us and inter=5000us: √(100×5000) = 707us. The generated tier_config.json contains this threshold in nanoseconds, used by topology_viz to draw the dendrogram cut line.", styles['Body']))
    story.append(Spacer(1, 0.2*inch))

    # Result collection
    img_buf = create_result_collection_diagram()
    story.append(KeepTogether([
        Paragraph("8c. Result Collection", styles['Section']),
        Paragraph("Results flow from controller container to host via Docker volume mount. The harness collect command waits for controller exit, creates timestamped output directory, copies result files, generates tier_config.json from tc params, and optionally runs topology_viz to produce visualization.", styles['Body']),
        Image(img_buf, width=7*inch, height=3.5*inch),
    ]))

    story.append(Paragraph("<b>Output Files:</b> latency_matrix.csv (NxN RTT in nanoseconds), bandwidth_matrix.csv (peak MB/s per pair), bandwidth_detailed.csv (all message sizes), tier_config.json (auto-generated thresholds), topology.svg (dendrogram visualization).", styles['Body']))

    story.append(PageBreak())

    # Error handling
    img_buf = create_error_handling_diagram()
    story.append(KeepTogether([
        Paragraph("9. Error Handling & Recovery", styles['Section']),
        Paragraph("Agents maintain heartbeat timestamp; controller detects dead agents via timeout. Failed pairs trigger configurable retry with exponential backoff. Graceful degradation skips unavailable agents and accepts partial results rather than aborting entire test.", styles['Body']),
        Image(img_buf, width=6.5*inch, height=2.8*inch),
    ]))

    story.append(PageBreak())

    # Implementation roadmap
    story.append(KeepTogether([
        Paragraph("10. Implementation Status & Next Steps", styles['Section']),
        Paragraph("<b>Completed:</b>", styles['Body']),
    ]))

    completed = [
        "Agent bootstrap: env var parsing, NIXL initialization, metadata upload to controller buffer",
        "Controller buffer management: allocation, header initialization, rendezvous wait loop",
        "PAIRWISE_LATENCY test: ping-pong protocol, timing measurement, results aggregation",
        "BANDWIDTH test with message size sweep: 1K, 4K, 16K, 64K, 256K, 1M, 4M bytes",
        "Bidirectional bandwidth testing (A->B and B->A)",
        "Container harness with Docker Compose for single-host testing",
        "CSV outputs: latency_matrix.csv, bandwidth_matrix.csv, bandwidth_detailed.csv"
    ]
    for bullet in completed:
        story.append(Paragraph(f"&bull; {bullet}", styles['Body']))

    story.append(Spacer(1, 0.1*inch))
    story.append(Paragraph("<b>Next Steps:</b>", styles['Body']))

    next_steps = [
        "LATENCY_SWEEP: Transfer time vs message size curves (8B-64KB, 1000 iterations per size)",
        "Topology inference: Hierarchical clustering from latency matrix",
        "Concurrent bandwidth bottleneck detection (optional Phase 2b)"
    ]
    for bullet in next_steps:
        story.append(Paragraph(f"&bull; {bullet}", styles['Body']))

    # Key design decisions table
    story.append(Spacer(1, 0.3*inch))
    story.append(KeepTogether([
        Paragraph("Key Design Decisions Summary", styles['Section']),
        Spacer(1, 0.1*inch),
    ]))

    decisions = [
        ['Decision', 'Choice', 'Rationale'],
        ['IPC Mechanism', 'NIXL-only (no TCP)', 'Simpler security model, container-friendly'],
        ['Agent spawn', 'Orchestrator + env vars', 'No handshake needed, metadata pre-shared'],
        ['Coordination', 'Polling-based', 'Reliable, works across all environments'],
        ['Buffer allocation', 'Upfront (256MB/agent)', 'Single metadata share, no per-test sync'],
        ['Bandwidth sweep', '7 message sizes (1K-4M)', 'Characterize full performance curve'],
        ['CSV output', 'Matrix + Detailed', 'Quick topology view + full measurement data'],
        ['Topology inference', 'Incremental levels', 'Quick results first, detail as needed'],
        ['Results upload', 'Initiator-only', 'Reduces data duplication']
    ]

    table = Table(decisions, colWidths=[1.8*inch, 2*inch, 3.2*inch])
    table.setStyle(TableStyle([
        ('BACKGROUND', (0, 0), (-1, 0), colors.darkblue),
        ('TEXTCOLOR', (0, 0), (-1, 0), colors.whitesmoke),
        ('FONTNAME', (0, 0), (-1, 0), 'Helvetica-Bold'),
        ('FONTSIZE', (0, 0), (-1, -1), 9),
        ('ALIGN', (0, 0), (-1, -1), 'LEFT'),
        ('GRID', (0, 0), (-1, -1), 0.5, colors.gray),
        ('BACKGROUND', (0, 1), (-1, -1), colors.beige),
        ('VALIGN', (0, 0), (-1, -1), 'MIDDLE'),
        ('LEFTPADDING', (0, 0), (-1, -1), 6),
        ('RIGHTPADDING', (0, 0), (-1, -1), 6),
        ('TOPPADDING', (0, 0), (-1, -1), 4),
        ('BOTTOMPADDING', (0, 0), (-1, -1), 4),
    ]))
    story.append(table)

    doc.build(story)
    print("PDF generated: /home/rarun/nixl-topo-disc/docs/NIXL_Topology_Discovery_Design.pdf")


if __name__ == "__main__":
    build_pdf()
