/*
Copyright (C) 2019  freepdk  https://free-pdk.github.io

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include "fpdkutil.h"
#include "fpdkcom.h"
#include "fpdkicdata.h"
#include "fpdkiccalib.h"
#include "fpdkihex8.h"
#include "argp.h"

const char *argp_program_version                = "easypdkprog 1.0";
static const char easypdkprog_doc[]             = "easypdkprog -- read, write and execute programs on PADAUK microcontroller\nhttps://free-pdk.github.io";
static const char easypdkprog_args_doc[]        = "list|probe|read|write|erase|start [FILE]";

static struct argp_option easypdkprog_options[] = {
  {"verbose",     'v', 0,      0,  "Verbose output" },
  {"port",        'p', "PORT", 0,  "COM port of programmer. Default: Auto search" },
  {"bin",         'b', 0,      0,  "Binary file output. Default: ihex8" },
  {"noerase",    555,  0,      0,  "Skip erase before write" },
  {"noblankchk", 666,  0,      0,  "Skip blank check before write" },
  {"securefill", 777,  0,      0,  "Fill unused space with 0 (NOP) to prevent readout" },
  {"noverify",   888,  0,      0,  "Skip verify after write" },
  {"nocalibrate",999,  0,      0,  "Skip calibration after write." },
  {"fuse",        'f', "FUSE", 0,  "FUSE value, e.g. 0x31FD"},
  {"runvdd",      'r', "VDD",  0,  "Voltage for running the IC. Default: 5.0" },
  {"icname",      'n', "NAME", 0,  "IC name, e.g. PFS154" },
  {"icid",        'i', "ID",   0,  "IC ID 12 bit, e.g. 0xAA1" },
  { 0 }
};

struct easypdkprog_args {
  char     command;
  int      verbose;
  char     *port;
  int      binout;
  char     *inoutfile;
  int      securefill;
  int      nocalibrate;
  int      noerase;
  int      noblankcheck;
  int      noverify;
  uint16_t fuse;
  float    runvdd;
  char     *ic;
  uint16_t icid;
};

static error_t easypdkprog_parse_opt(int key, char *arg, struct argp_state *state)
{
  struct easypdkprog_args *arguments = state->input;
  switch (key)
  {
    case 'v': arguments->verbose = 1; break;
    case 'p': arguments->port = arg; break;
    case 'b': arguments->binout = 1; break;
    case 555: arguments->noerase = 1; break;
    case 666: arguments->noblankcheck = 1; break;
    case 777: arguments->securefill = 1; break;
    case 888: arguments->noverify = 1; break;
    case 999: arguments->nocalibrate = 1; break;
    case 'f': if(arg) arguments->fuse = strtol(arg,NULL,16); break;
    case 'n': arguments->ic = arg; break;
    case 'i': if(arg) arguments->icid = strtol(arg,NULL,16); break;
    case 'r': if(arg) sscanf(arg,"%f",&arguments->runvdd); break;

    case ARGP_KEY_ARG:
      if(0 == state->arg_num)
      {
        if( !strcmp(arg,"list") && 
            !strcmp(arg,"probe") && 
            !strcmp(arg,"read") && 
            !strcmp(arg,"write") && 
            !strcmp(arg,"erase") && 
            !strcmp(arg,"start") )
        {
          argp_usage(state);
        }
        arguments->command = arg[0];
      }
      else if(1 == state->arg_num)
      {
        arguments->inoutfile = arg;
      }
      else
        argp_usage(state);
      break;

    case ARGP_KEY_END:
      if (state->arg_num < 1)
        argp_usage(state);
      break;

    default:
      return ARGP_ERR_UNKNOWN;
  }
  return 0;
}

static struct argp argp = { easypdkprog_options, easypdkprog_parse_opt, easypdkprog_args_doc, easypdkprog_doc };

int main( int argc, const char * argv [] )
{
  //immediate output on stdout (no buffering)
  setvbuf(stdout,0,_IONBF,0);
  
  struct easypdkprog_args arguments = { .runvdd=5.0, .fuse=0xFFFF };
  argp_parse(&argp, argc, (char**)argv, 0, 0, &arguments);

  verbose_set(arguments.verbose);

  if( 'l'==arguments.command )
  {
    printf("Supported ICs:\n");
    for( uint16_t id=1; id<0xFFF; id++ )
    {
      FPDKICDATA* icdata = FPDKICDATA_GetICDataById12Bit(id);
      if( icdata )
      {
        printf(" %-8s (0x%03X): %s: %d (%d bit), RAM: %3d bytes\n", icdata->name, icdata->id12bit, (FPDK_IC_FLASH==icdata->type)?"FLASH":"OTP  ", icdata->codewords, icdata->codebits, icdata->ramsize);
        if( icdata->name_variant_1[0] )
          printf(" %-8s (0x%03X): %s: %d (%d bit), RAM: %3d bytes\n", icdata->name_variant_1, icdata->id12bit, (FPDK_IC_FLASH==icdata->type)?"FLASH":"OTP  ", icdata->codewords, icdata->codebits, icdata->ramsize);
        if( icdata->name_variant_2[0] )
          printf(" %-8s (0x%03X): %s: %d (%d bit), RAM: %3d bytes\n", icdata->name_variant_2, icdata->id12bit, (FPDK_IC_FLASH==icdata->type)?"FLASH":"OTP  ", icdata->codewords, icdata->codebits, icdata->ramsize);
      }
    }
    return 0;
  }

  //pre checks
  FPDKICDATA* icdata = NULL;
  if( ('r'==arguments.command) || ('w'==arguments.command) || ('e'==arguments.command) )
  {
    if( !arguments.icid && !arguments.ic)
    {
      printf("ERROR: IC NAME and OTP ID unspecified. Use -n or -o option.\n");
      return -2;
    }
    else
    {
      icdata = FPDKICDATA_GetICDataById12Bit(arguments.icid);
      if( !icdata )
        icdata = FPDKICDATA_GetICDataByName(arguments.ic);

      if( !icdata )
      {
        printf("ERROR: Unknown OTP ID.\n");
        return -2;
      }
    }
  }

  if( ('w'==arguments.command) && !arguments.inoutfile )
  {
    printf("ERROR: Write requires an input file.\n");
    return -2;
  }

  //open programmer
  int comfd = -1;
  if( !arguments.port )
  {
    verbose_printf("Searching programmer...");
    char compath[64];
    comfd = FPDKCOM_OpenAuto(compath);
    if( comfd<0 )
      printf("No programmer found\n");
    else
      verbose_printf(" found: %s\n", compath);
  }
  else
  {
    comfd = FPDKCOM_Open(arguments.port);
    if( comfd<0 )
      printf("Error %d connecting to programmer on port: %s\n\n", comfd, arguments.port);
  }

  if( comfd<0 )
    return -1;

  float hw,sw,proto;
  if( !FPDKCOM_GetVersion(comfd, &hw, &sw, &proto) )
    return -1;

  verbose_printf("FREE-PDK EASY PROG - Hardware:%.1f Firmware:%.1f Protocol:%.1f\n", hw, sw, proto);

  switch( arguments.command )
  {
    case 'p': //probe
    {
      printf("Probing IC... ");
      FPDKICTYPE type;
      float vpp,vdd;
      int icid = FPDKCOM_IC_Probe(comfd,&vpp,&vdd,&type);
      if( icid<=0 )
        printf("Nothing found.\n");      
      else
      if( (icid>=FPDK_ERR_ERROR) && (icid<=0xFFFF) )
        printf("ERROR: %s\n",FPDK_ERR_MSG[icid&0x000F]);
      else
      {
        printf("found.\nTYPE:%s RSP:0x%X VPP=%.2f VDD=%.2f\n",(FPDK_IC_FLASH==type)?"FLASH":"OTP",icid,vpp,vdd);

        if( FPDK_IC_FLASH==type )
          icdata = FPDKICDATA_GetICDataById12Bit(icid);
        else
          icdata = FPDKICDATA_GetICDataForOTPByCmdResponse(icid);

        if( icdata )
        {
          printf("IC is supported: %s", icdata->name);
          if( icdata->name_variant_1[0] )
            printf(" / %s", icdata->name_variant_1);
          if( icdata->name_variant_2[0] )
            printf(" / %s", icdata->name_variant_2);

          printf(" ICID:0x%03X", icdata->id12bit);
        }
        else
          printf("Unsupported IC");

        printf("\n");
      }
    }
    break;

    case 'r': //read
    {
      printf("Reading IC... ");
      int r = FPDKCOM_IC_Read(comfd, icdata->id12bit, icdata->type, icdata->vdd_cmd_read, icdata->vpp_cmd_read, 0, icdata->addressbits, 0, icdata->codebits, icdata->codewords);
      if( r>=FPDK_ERR_ERROR )
        printf("FPDK_ERROR: %s\n",FPDK_ERR_MSG[r&0x000F]);
      else
      if( r != icdata->id12bit )
        printf("ERROR: Read failed.\n");
      else
      {
        printf("done.\n");
        if( arguments.inoutfile )
        {
          uint8_t buf[0x1800*2];
          if( FPDKCOM_GetBuffer(comfd, 0, buf, icdata->codewords*sizeof(uint16_t))>0 )
          {
            if( arguments.binout )
            {
              FILE *f = fopen(arguments.inoutfile,"wb");
              if(f)
              {
                fwrite(buf, 1, icdata->codewords*sizeof(uint16_t), f);
                fclose(f);
              }
              else
                printf("ERROR: Could not write file: %s\n", arguments.inoutfile);
            }
            else
            {
              if( FPDKIHEX8_WriteFile(arguments.inoutfile, buf, icdata->codewords*sizeof(uint16_t)) < 0 )
                printf("ERROR: Could not write file: %s\n", arguments.inoutfile);
            }
          }
          else
            printf("ERROR: Could not read data from programmer\n");
        }
      }
    }
    break;

    case 'w': //write
    {
      uint16_t write_data[0x1800];
      if( FPDKIHEX8_ReadFile(arguments.inoutfile, write_data, 0x1800) < 0 )
      {
        printf("ERROR: Invalid input file / not ihex8 format.\n");
        break;
      }

      if( (FPDK_IC_FLASH == icdata->type) && !arguments.noerase )
      {
        printf("Erasing IC... ");

        int r = FPDKCOM_IC_Erase(comfd, icdata->id12bit, icdata->type, icdata->vdd_cmd_erase, icdata->vpp_cmd_erase, icdata->vdd_erase_hv, icdata->vpp_erase_hv, icdata->erase_clocks );
        if( r>=FPDK_ERR_ERROR )
          printf("FPDK_ERROR: %s\n",FPDK_ERR_MSG[r&0x000F]);
        else
        if( r != icdata->id12bit )
          printf("ERROR: Erase failed.\n");
        else
          printf("done.\n");
      }

      if( !arguments.noblankcheck )
      {
        verbose_printf("Blank check IC... ");
        int r = FPDKCOM_IC_BlankCheck(comfd, icdata->id12bit, icdata->type, icdata->vdd_cmd_read, icdata->vpp_cmd_read, icdata->addressbits, icdata->codebits, icdata->codewords, icdata->exclude_code_first_instr, icdata->exclude_code_start, icdata->exclude_code_end);
        if( r>=FPDK_ERR_ERROR )
        {
          printf("FPDK_ERROR: %s\n",FPDK_ERR_MSG[r&0x000F]);
          break;
        }
        if( r != icdata->id12bit )
        {
          printf("ERROR: Blank check failed.\n");
          break;
        }
        verbose_printf("done.\n");
      }

      //TODO: OPTIMIZE: loop through data and only write used regions

      //QUICK IMPLEMENTATION FOR NOW: find end of data and program everything
      uint8_t data[0x1800];
      memset(data, arguments.securefill?0x00:0xFF, sizeof(data));
      uint32_t len = 0;
      for( uint32_t p=0; p<sizeof(data); p++)
      {
        if( write_data[p] & 0xFF00 )
        {
          data[p] = write_data[p]&0xFF;
          len = p;
        }
      }

      if( arguments.securefill )
      {
        uint16_t fillend = icdata->codewords - 8;
        if( icdata->exclude_code_start && (icdata->exclude_code_start < fillend) )
          fillend = icdata->exclude_code_start;

        len = fillend*sizeof(uint16_t);
      }

      if( 0 == len )
      {
        printf("Nothing to write\n");
        break;
      }

      bool do_calibration = false;
      
      uint32_t       calibrate_frequency = 0;
      uint32_t       calibrate_millivolt = 5000;
      FPDKCALIBTYPE  calibrate_prg_type;
      uint8_t        calibrate_prg_algo;
      uint32_t       calibrate_prg_loopcycles;
      uint16_t       calibrate_prg_pos;

      if( !arguments.nocalibrate )
        do_calibration = FPDKCALIB_InsertCalibration(icdata, data, len, &calibrate_frequency, &calibrate_millivolt, &calibrate_prg_type, &calibrate_prg_algo, &calibrate_prg_loopcycles, &calibrate_prg_pos);

      printf("Writing IC... ");

      if( !FPDKCOM_SetBuffer(comfd, 0, data, len) )
      {
        printf("ERROR: Could not send data to programmer\n");
        break;
      }

      uint32_t codewords = (len+1)/2;

      int r = FPDKCOM_IC_Write(comfd, icdata->id12bit, icdata->type, 
                               icdata->vdd_cmd_write, icdata->vpp_cmd_write, icdata->vdd_write_hv, icdata->vpp_write_hv,
                               0, icdata->addressbits, 0, icdata->codebits, codewords, 
                               icdata->write_block_size, icdata->write_block_clock_groups, icdata->write_block_clocks_per_group);
      if( r>=FPDK_ERR_ERROR )
      {
        printf("FPDK_ERROR: %s\n",FPDK_ERR_MSG[r&0x000F]);
        break;
      }
      if( r != icdata->id12bit )
      {
        printf("ERROR: Write failed.\n");
        break;
      }
      printf("done.\n");

      if( !arguments.noverify )
      {
        verbose_printf("Verifiying IC... ");
        int r = FPDKCOM_IC_Verify(comfd, icdata->id12bit, icdata->type, icdata->vdd_cmd_read, icdata->vpp_cmd_read, 0, icdata->addressbits, 0, icdata->codebits, codewords, icdata->exclude_code_first_instr, icdata->exclude_code_start, icdata->exclude_code_end);
        if( r>=FPDK_ERR_ERROR )
        {
          printf("FPDK_ERROR: %s\n",FPDK_ERR_MSG[r&0x000F]);
          break;
        }
        if( r != icdata->id12bit )
        {
          printf("ERROR: Verify failed.\n");
          break;
        }
        verbose_printf("done.\n");
      }

      if( 0xFFFF != arguments.fuse )
      {
        printf("Writing IC Fuse... ");
        uint8_t fusedata[] = {arguments.fuse, arguments.fuse>>8};

        uint16_t fuseaddr = icdata->codewords-1;

        if( !FPDKCOM_SetBuffer(comfd, fuseaddr*2, fusedata, sizeof(fusedata)) )
        {
          printf("ERROR: Could not send data to programmer\n");
          break;
        }

        int r = FPDKCOM_IC_Write(comfd, icdata->id12bit, icdata->type, 
                                 icdata->vdd_cmd_write, icdata->vpp_cmd_write, icdata->vdd_write_hv, icdata->vpp_write_hv,
                                 fuseaddr, icdata->addressbits, fuseaddr, icdata->codebits, 1, 
                                 icdata->write_block_size, icdata->write_block_clock_groups, icdata->write_block_clocks_per_group);
        if( r>=FPDK_ERR_ERROR )
        {
          printf("FPDK_ERROR: %s\n",FPDK_ERR_MSG[r&0x000F]);
          break;
        }
        if( r != icdata->id12bit )
        {
          printf("ERROR: Write fuse failed.\n");
          break;
        }
        printf("done.\n");
      }

      if( do_calibration )
      {
        printf("Calibrating IC (@%.2fV ", (float)calibrate_millivolt/1000.0);
        switch( calibrate_prg_type )
        {
          case FPDKCALIB_IHRC:         printf("IHRC SYSCLK=%dHz", calibrate_frequency); break;
          case FPDKCALIB_ILRC:         printf("ILRC SYSCLK=%dHz", calibrate_frequency); break;
          case FPDKCALIB_BG:           printf("BG"); break;
          case FPDKCALIB_IHRC_BG:      printf("BG / IHRC SYSCLK=%dHz", calibrate_frequency); break;
          case FPDKCALIB_ILRC_BG:      printf("BG / ILRC SYSCLK=%dHz", calibrate_frequency); break;
        }
        printf(")... ");
        
        uint8_t fcalval, bgcalval;
        uint32_t fcalfreq;

        if( !FPDKCOM_IC_Calibrate(comfd, calibrate_prg_type, calibrate_millivolt, calibrate_frequency, calibrate_prg_loopcycles, &fcalval, &fcalfreq, &bgcalval) )
        {
          printf("failed.\n");
          break;
        }

        switch( calibrate_prg_type )
        {
          case FPDKCALIB_BG: 
            break;
          case FPDKCALIB_IHRC:         
          case FPDKCALIB_ILRC:
          case FPDKCALIB_IHRC_BG:
          case FPDKCALIB_ILRC_BG:
            printf("calibration result: %dHz (0x%02X)  ", fcalfreq, fcalval); 
            break;
        }

        if( FPDKCALIB_RemoveCalibration(calibrate_prg_algo, data, calibrate_prg_pos, fcalval) )
        {
          //TODO: OPTIMIZE: only write part
          if( !FPDKCOM_SetBuffer(comfd, 0, data, len) )
          {
            printf("ERROR: Could not send data to programmer\n");
            break;
          }

          uint32_t codewords = (len+1)/2;

          int r = FPDKCOM_IC_Write(comfd, icdata->id12bit, icdata->type, 
                                   icdata->vdd_cmd_write, icdata->vpp_cmd_write, icdata->vdd_write_hv, icdata->vpp_write_hv,
                                   0, icdata->addressbits, 0, icdata->codebits, codewords, 
                                   icdata->write_block_size, icdata->write_block_clock_groups, icdata->write_block_clocks_per_group);
          if( r>=FPDK_ERR_ERROR )
          {
            printf("FPDK_ERROR: %s\n",FPDK_ERR_MSG[r&0x000F]);
            break;
          }
          if( r != icdata->id12bit )
          {
            printf("ERROR: Write calibration failed.\n");
            break;
          }
        }
        else
        {
          printf("ERROR: Removing calibration function.\n");
          break;
        }
        printf("done.\n");
      }
    }
    break;

    case 'e': //erase
    {
      if( FPDK_IC_FLASH != icdata->type )
      {
        printf("ERROR: Only FLASH type IC can get erased\n");
        break;
      }

      printf("Erasing IC... ");
      int r = FPDKCOM_IC_Erase(comfd, icdata->id12bit, icdata->type, icdata->vdd_cmd_erase, icdata->vpp_cmd_erase, icdata->vdd_erase_hv, icdata->vpp_erase_hv, icdata->erase_clocks );
      if( r>=FPDK_ERR_ERROR )
        printf("FPDK_ERROR: %s\n",FPDK_ERR_MSG[r&0x000F]);
      else
      if( r != icdata->id12bit )
        printf("ERROR: Erasing IC failed.\n");
      else
      {
        printf("done.\n");

        if( !arguments.noblankcheck )
        {
          verbose_printf("Blank check IC... ");
          int r = FPDKCOM_IC_BlankCheck(comfd, icdata->id12bit, icdata->type, icdata->vdd_cmd_read, icdata->vpp_cmd_read, icdata->addressbits, icdata->codebits, icdata->codewords, icdata->exclude_code_first_instr, icdata->exclude_code_start, icdata->exclude_code_end);
          if( r>=FPDK_ERR_ERROR )
          {
            printf("FPDK_ERROR: %s\n",FPDK_ERR_MSG[r&0x000F]);
            break;
          }
          if( r != icdata->id12bit )
          {
            printf("ERROR: Blank check IC failed.\n");
            break;
          }
          verbose_printf("done.\n");
        }
      }
    }
    break;

    case 's':
    {
      printf("Running IC (%.2fV)... ", arguments.runvdd);
      if( !FPDKCOM_IC_StartExecution(comfd, arguments.runvdd) )
      {
        printf("ERROR: Could not start IC.\n");
        break;
      }
      printf("IC started, press [Esc] to stop.\n");

      for( ;; )
      {
        fpdkutil_waitfdorkeypress(comfd, 1000);
        unsigned char dbgmsg[256] = {0};
        int dbgd = FPDKCOM_IC_ReceiveDebugData(comfd, dbgmsg, 255);
        if( dbgd>0 )
        {
          dbgmsg[dbgd]=0;
          printf("%s", dbgmsg);
        }
        int c = fpdkutil_getchar();
        if( 27 == c )
          break;
        if( -1 != c )
          FPDKCOM_IC_SendDebugData(comfd, (uint8_t*)&c, 1);
      }

      if( FPDKCOM_IC_StopExecution(comfd) )
        printf("\nIC stopped\n");
    }
    break;

  }

  return 0;
}
