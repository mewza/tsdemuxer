import * as fs from 'fs';
import { TSDemuxer } from './demuxer';
import { PATProgram, PMTStream } from './types';

/**
 * Example usage of the TS Demuxer
 * 
 * This example shows how to:
 * 1. Create a demuxer instance
 * 2. Listen for events
 * 3. Parse a .ts file
 * 4. Get program information
 */

function main() {
  // Create a new demuxer instance
  const demuxer = new TSDemuxer();

  // Listen for PAT (Program Association Table) events
  demuxer.on('pat', (pat) => {
    console.log('PAT received:');
    console.log(`  Transport Stream ID: ${pat.transportStreamId}`);
    console.log(`  Version: ${pat.versionNumber}`);
    console.log(`  Programs: ${pat.programs.length}`);
    pat.programs.forEach((program: PATProgram) => {
      console.log(`    - Program ${program.programNumber}: PMT PID 0x${program.pid.toString(16)}`);
    });
  });

  // Listen for PMT (Program Map Table) events
  demuxer.on('pmt', (pmt) => {
    console.log(`\nPMT received for program ${pmt.programNumber}:`);
    console.log(`  PCR PID: 0x${pmt.pcrPid.toString(16)}`);
    console.log(`  Streams: ${pmt.streams.length}`);
    pmt.streams.forEach((stream: PMTStream) => {
      console.log(`    - PID 0x${stream.pid.toString(16)}: Type 0x${stream.streamType.toString(16)}`);
    });
  });

  // Listen for PES (Packetized Elementary Stream) events
  demuxer.on('pes', ({ pid, pes }) => {
    console.log(`\nPES packet on PID 0x${pid.toString(16)}:`);
    console.log(`  Stream ID: 0x${pes.streamId.toString(16)}`);
    console.log(`  Packet Length: ${pes.packetLength}`);
    if (pes.pts !== undefined) {
      console.log(`  PTS: ${pes.pts}`);
    }
    if (pes.dts !== undefined) {
      console.log(`  DTS: ${pes.dts}`);
    }
    console.log(`  Data Length: ${pes.data.length} bytes`);
  });

  // Listen for raw TS packets (optional - can be very verbose)
  let packetCount = 0;
  demuxer.on('packet', (packet) => {
    packetCount++;
    if (packetCount % 100 === 0) {
      console.log(`Processed ${packetCount} packets...`);
    }
  });

  // Example: Read from a .ts file
  const filename = process.argv[2];
  if (!filename) {
    console.log('Usage: node example.js <file.ts>');
    console.log('\nAlternatively, pipe data: cat file.ts | node example.js');
    return;
  }

  console.log(`Reading TS file: ${filename}\n`);

  // Read file in chunks
  const stream = fs.createReadStream(filename, { highWaterMark: 188 * 1000 }); // Read ~1000 packets at a time

  stream.on('data', (chunk: string | Buffer) => {
    if (typeof chunk === 'string') {
      chunk = Buffer.from(chunk);
    }
    demuxer.push(chunk);
  });

  stream.on('end', () => {
    console.log(`\n\nFile processing complete!`);
    console.log(`Total packets processed: ${packetCount}`);
    
    // Get program information
    const programs = demuxer.getPrograms();
    console.log(`\nProgram Summary:`);
    programs.forEach((program) => {
      console.log(`\nProgram ${program.programNumber}:`);
      console.log(`  PMT PID: 0x${program.pmtPid.toString(16)}`);
      if (program.pcrPid) {
        console.log(`  PCR PID: 0x${program.pcrPid.toString(16)}`);
      }
      console.log(`  Streams:`);
      program.streams.forEach((stream) => {
        console.log(`    - PID 0x${stream.pid.toString(16)}: ${stream.streamTypeName}`);
      });
    });
  });

  stream.on('error', (err) => {
    console.error('Error reading file:', err);
  });
}

// Run if executed directly
if (require.main === module) {
  main();
}

export { main };
