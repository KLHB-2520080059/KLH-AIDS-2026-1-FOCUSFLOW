const express = require('express');
const router = express.Router();

// Course outcomes from the operating systems syllabus.
// project: true marks the topics this scheduler project uses.
const OUTCOMES = [
  { id: 'CO-1', title: 'The OS as a service layer', project: false, topics: [
    'Layered service abstraction', 'User space and kernel space', 'System calls and kernel services',
    'The shell as a user-space program', 'Command execution journey', 'Linux architecture and software stack'] },
  { id: 'CO-2', title: 'Processes and process control', project: true, topics: [
    'Process abstraction', 'Lifecycle and state transitions', 'Creation, execution, synchronization and termination',
    'User-level process scheduling concepts', 'Common process management pitfalls'] },
  { id: 'CO-3', title: 'Inter-process communication', project: false, topics: [
    'Anonymous pipes', 'Named pipes (FIFOs)', 'Signals and asynchronous notifications',
    'Signal handlers and POSIX signal programming', 'Process groups, sessions and job control'] },
  { id: 'CO-4', title: 'Memory management', project: false, topics: [
    'Virtual memory abstraction', 'Address translation and page tables', 'Page faults and demand paging',
    'Linux process address space layout', 'Dynamic memory allocation', 'Copy-on-write and process creation',
    'Memory errors and OS responses', 'Memory debugging and analysis tools'] },
  { id: 'CO-5', title: 'File systems and file I/O', project: false, topics: [
    'Unix file abstraction, inodes and directory entries', 'Virtual file system (VFS)',
    'File descriptors and open file management', 'Linux file I/O system calls', 'Buffered and unbuffered I/O',
    'Memory-mapped file I/O', 'ext4 file system overview'] },
  { id: 'CO-6', title: 'Concurrency and synchronization', project: true, topics: [
    'Threads and processes, POSIX threads', 'Race conditions and shared data', 'Mutual exclusion using mutexes',
    'Condition variables and thread coordination', 'Counting semaphores', 'Deadlocks and concurrency hazards',
    'Advanced synchronization concepts'] }
];

// GET /api/syllabus (public)
router.get('/', (req, res) => res.json({ outcomes: OUTCOMES }));

module.exports = router;
