/* vim: set syntax=c expandtab sw=2 softtabstop=2 autoindent smartindent smarttab : */
/*
 * Arbritrary wheel pattern generator wheel definitions
 *
 * copyright 2014 David J. Andruczyk
 *
 * Ardu-Stim software is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * ArduStim software is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with any ArduStim software.  If not, see http://www.gnu.org/licenses/
 *
 */
#ifndef __WHEEL_DEFS_H__
#define __WHEEL_DEFS_H__

#include "globals.h"

#if defined(__AVR__)
#include <avr/pgmspace.h> // For PROGMEM on AVR
#endif

/**
 * @brief Optimized RLE decoder for wheel patterns with minimal RAM usage
 *
 * @param pattern Pointer to the RLE-encoded pattern in PROGMEM
 * @param index The index to retrieve
 * @return uint8_t The value at the specified index, or 0 if index is out of bounds
 */
inline uint8_t decode_wheel_pattern(const unsigned char *pattern, uint16_t index)
{
    uint16_t pos = 0;
    uint16_t currentIndex = 0;

    while (1) // No arbitrary counter limit
    {
        // Read pattern length and check for end marker
        uint8_t patternLength = pgm_read_byte(&pattern[pos]);
        if (patternLength == 0 || patternLength > 16)
            return 0; // End of pattern marker or invalid length
        
        pos++;

        // Position of pattern values
        uint16_t valuePos = pos;
        pos += patternLength; // Skip past values

        // Read repeat count (must be > 0)
        uint8_t repeatCount = pgm_read_byte(&pattern[pos++]);
        
        uint16_t sectionSize = patternLength * repeatCount;

        // Check if our target index is in this section
        if (currentIndex + sectionSize > index)
        {
            // Calculate offset within pattern
            uint16_t patternOffset = (index - currentIndex) % patternLength;
            return pgm_read_byte(&pattern[valuePos + patternOffset]);
        }

        // Move to next section
        currentIndex += sectionSize;
    }
}

/* Wheel patterns!
 *
 * Wheel patterns define the pin states and specific times. The ISR runs
 * at a constant speed related to the requested RPM. The request RPM is
 * scaled based on the LENGTH of each wheel's array.  The reference is
 * the 60-2 which was the first decoder designed which has 120 "edges"
 * (transitions" for each revolution of the wheel. Any other wheel that
 * also has 120 edges has and RPM scaling factor of 1.0. IF a wheel has
 * less edges needed to "describe" it, it's number of edges are divided by 120 to
 * get the scaling factor which is applied to the RPM calculation.
 * There is an enumeration (below) that lists the defined wheel types,
 * as well as an array listing the rpm_scaling factors with regards to
 * each pattern.
 *
 * NOTE: There is MORE THAN ONE WAY to define a wheel pattern.  You can
 * use more edges to get to 1 deg accuracy but the side effect is that
 * your maximum RPM is capped because of that. Currently 60-2 can run
 * up to about 60,000 RPM, 360and8 can only do about 10,000 RPM becasue
 * it has 6x the number of edges...  The less edges, the faster it can go... :)
 *
 * Using more edges allows you to do things like vary the dutycycle,
 * i.e. a simple non-missing tooth 50% duty cycle wheel can be defined
 * with only 2 entries if you really want, but I didn't do it that way
 * for some of the simple ones as it made it seem somewhat confusing
 * to look at as it required you to keep the rpm_scaler factor in mind.
 * Most/all patterns show the pulses you're receive for one revolution
 * of a REAL wheel on a real engine.
 */

/*
 * Values to use within a wheel definition.
 * 0 means no tooth on any wheel definition
 * 1 means crank tooth
 * 2 means cam1 tooth
 * 4 means cam2 tooth
 *
 * combinations of numbers mean all of the related teeth are present,
 * eg  3 means crank and cam1, 5 means crank and cam2, 6 means cam1 and cam2, 7 means crank, cam1 and cam2
 */

/** @enum WheelType
 * @brief Enumerates supported wheel patterns
 * Wheel types we know about...
 * This enumerations is the INDEX into the Wheels[] array of structures
 * defined in main file. That struct contains pointers to the following:
 * wheel name in a user friendly string
 * pointer to the wheel edge array used by the ISR
 * RPM scaling factor (num_edges/120 for crank wheels)
 * Number of edges in the edge array above, needed by the ISR
 */
typedef enum
{
    DIZZY_FOUR_CYLINDER,               /* 2 evenly spaced teeth */
    DIZZY_SIX_CYLINDER,                /* 3 evenly spaced teeth */
    DIZZY_EIGHT_CYLINDER,              /* 4 evenly spaced teeth */
    SIXTY_MINUS_TWO,                   /* 60-2 crank only */
    SIXTY_MINUS_TWO_WITH_CAM,          /* 60-2 with 2nd trigger on cam */
    SIXTY_MINUS_TWO_WITH_HALFMOON_CAM, /* 60-2 with "half moon" trigger on cam */
    THIRTY_SIX_MINUS_ONE,              /* 36-1 crank only */
    TWENTY_FOUR_MINUS_ONE,
    FOUR_MINUS_ONE_WITH_CAM,                                  /* 4-1 crank + cam */
    EIGHT_MINUS_ONE,                                          /* 8-1 crank only */
    SIX_MINUS_ONE_WITH_CAM,                                   /* 6-1 crank + cam */
    TWELVE_MINUS_ONE_WITH_CAM,                                /* 12-1 crank + cam */
    FOURTY_MINUS_ONE,                                         /* Ford V-10 40-1 crank only */
    DIZZY_FOUR_TRIGGER_RETURN,                                /* dizzy 4 cylinder signal, 40deg on 50 deg off */
    ODDFIRE_VR,                                               /* Oddfire V-twin */
    OPTISPARK_LT1,                                            /* Optispark 360 and 8 */
    TWELVE_MINUS_THREE,                                       /* 12-3 */
    THIRTY_SIX_MINUS_TWO_TWO_TWO,                             /* 36-2-2-2 crank only H4 */
    THIRTY_SIX_MINUS_TWO_TWO_TWO_H6,                          /* 36-2-2-2 crank only H6 */
    THIRTY_SIX_MINUS_TWO_TWO_TWO_WITH_CAM,                    /* 36-2-2-2 crank and cam */
    FOURTY_TWO_HUNDRED_WHEEL,                                 /* 4200 wheel */
    THIRTY_SIX_MINUS_ONE_WITH_CAM_FE3,                        /* Mazda F3 36-1 crank and cam */
    SIX_G_SEVENTY_TWO_WITH_CAM,                               /* Mitsubishi DOHC CAS and TCDS 6G72 */
    BUELL_ODDFIRE_CAM,                                        /* Buell 45 deg cam wheel */
    GM_LS1_CRANK_AND_CAM,                                     /* GM LS1 24 tooth with cam */
    GM_58x_LS_CRANK_4X_CAM,                                   /* GM 58x LS crank 4x cam wheel */
    LOTUS_THIRTY_SIX_MINUS_ONE_ONE_ONE_ONE,                   /* Lotus crank wheel 36-1-1-1-1 */
    HONDA_RC51_WITH_CAM,                                      /* Honda oddfire 90 deg V-twin */
    THIRTY_SIX_MINUS_ONE_WITH_SECOND_TRIGGER,                 /* From jimstim */
    CHRYSLER_NGC_THIRTY_SIX_PLUS_TWO_MINUS_TWO_WITH_NGC4_CAM, /* Chrysler NGC 36+2-2 crank with NGC 4 cylinder cam pattern */
    CHRYSLER_NGC_THIRTY_SIX_MINUS_TWO_PLUS_TWO_WITH_NGC6_CAM, /* Chrysler NGC 36-2+2 crank with NGC 6 cylinder cam pattern */
    CHRYSLER_NGC_THIRTY_SIX_MINUS_TWO_PLUS_TWO_WITH_NGC8_CAM, /* Chrysler NGC 36-2+2 crank with NGC 8 cylinder cam pattern */
    WEBER_IAW_WITH_CAM,                                       /* From jimstim IAW weber-marelli */
    FIAT_ONE_POINT_EIGHT_SIXTEEN_VALVE_WITH_CAM,              /* Fiat 1.8 16V from jimstim */
    THREE_SIXTY_NISSAN_CAS,                                   /*from jimstim 360 tooth cas with 6 slots */
    TWENTY_FOUR_MINUS_TWO_WITH_SECOND_TRIGGER,                /* Mazda CAS 24-1 inner ring single pulse outer ring */
    YAMAHA_EIGHT_TOOTH_WITH_CAM,                              /* 02-03 Yamaha R1, seank */
    GM_FOUR_TOOTH_WITH_CAM,                                   /* GM 4 even crank with half moon cam */
    GM_SIX_TOOTH_WITH_CAM,                                    /* GM 4 even crank with half moon cam */
    GM_EIGHT_TOOTH_WITH_CAM,                                  /* GM 4 even crank with half moon cam */
    VOLVO_D12ACD_WITH_CAM,                                    /* Volvo Diesel d12[acd] with cam (alex32 on forums.libreems.org */
    MAZDA_THIRTY_SIX_MINUS_TWO_TWO_TWO_WITH_SIX_TOOTH_CAM,
    MITSUBISH_4g63_4_2,
    AUDI_135_WITH_CAM,
    HONDA_D17_NO_CAM,
    MAZDA_323_AU,
    DAIHATSU_3CYL,
    MIATA_9905,
    TWELVE_WITH_CAM,                   // 12 evenly spaced crank teeth and a single cam tooth
    TWENTY_FOUR_WITH_CAM,              // 24 evenly spaced crank teeth and a single cam tooth
    SUBARU_SIX_SEVEN,                  /* Subaru 6 crank, 7 cam */
    GM_7X,                             /* GM 7X pattern. 6 even teeth with 1 extra uneven tooth */
    FOUR_TWENTY_A,                     /* DSM 420a */
    FORD_ST170,                        /* Ford ST170 */
    MITSUBISHI_3A92,                   /* Mitsubishi 3cylinder 3A92 */
    TOYOTA_4AGE_CAS,                   /*Toyota 4AGE CAS, 4 teeth and one cam tooth*/
    TOYOTA_4AGZE,                      /*Toyota 4AGZE, 24 teeth and one cam tooth*/
    SUZUKI_DRZ400,                     /* Suzuki DRZ-400 6 coil "tooths", 2 uneven crank tooths */
    JEEP2000_4CYL,                     /* Jeep 2.5 4cyl aka jeep2000_4cyl */
    JEEP2000_6CYL,                     /* Jeep 4.0 6cyl aka jeep2000_6cyl */
    BMW_N20,                           // BMW N20 58x and custom cam wheels
    VIPER_96_02,                       // Dodge Viper 1996-2002 wheel pattern
    THIRTY_SIX_MINUS_TWO_WITH_ONE_CAM, // 36-2 with  1 tooth cam - 2jz-gte VVTI crank pulley + non-vvti cam
    GM_40_OSS,                         // GM 40 tooth wheel no skips for transmission OSS simulation

    MAX_WHEELS,
} WheelType;

/* Name strings for EACH wheel type, for serial UI */
const char dizzy_four_cylinder_friendly_name[] PROGMEM = "4 cylinder dizzy";
const char dizzy_six_cylinder_friendly_name[] PROGMEM = "6 cylinder dizzy";
const char dizzy_eight_cylinder_friendly_name[] PROGMEM = "8 cylinder dizzy";
const char sixty_minus_two_friendly_name[] PROGMEM = "60-2 crank only";
const char sixty_minus_two_with_cam_friendly_name[] PROGMEM = "60-2 crank and cam";
const char sixty_minus_two_with_halfmoon_cam_friendly_name[] PROGMEM = "60-2 crank and 'half moon' cam";
const char thirty_six_minus_one_friendly_name[] PROGMEM = "36-1 crank only";
const char twenty_four_minus_one_friendly_name[] PROGMEM = "24-1 crank only";
const char four_minus_one_with_cam_friendly_name[] PROGMEM = "4-1 crank wheel with cam";
const char eight_minus_one_friendly_name[] PROGMEM = "8-1 crank only (R6)";
const char six_minus_one_with_cam_friendly_name[] PROGMEM = "6-1 crank with cam";
const char twelve_minus_one_with_cam_friendly_name[] PROGMEM = "12-1 crank with cam";
const char fourty_minus_one_friendly_name[] PROGMEM = "40-1 crank only (Ford V10)";
const char dizzy_four_trigger_return_friendly_name[] PROGMEM = "Distributor style 4 cyl 50deg off, 40 deg on";
const char oddfire_vr_friendly_name[] PROGMEM = "odd fire 90 deg pattern 0 and 135 pulses";
const char optispark_lt1_friendly_name[] PROGMEM = "GM OptiSpark LT1 360 and 8";
const char twelve_minus_three_friendly_name[] PROGMEM = "12-3 oddball";
const char thirty_six_minus_two_two_two_friendly_name[] PROGMEM = "36-2-2-2 H4 Crank only";
const char thirty_six_minus_two_two_two_h6_friendly_name[] PROGMEM = "36-2-2-2 H6 Crank only";
const char thirty_six_minus_two_two_two_with_cam_friendly_name[] PROGMEM = "36-2-2-2 Crank and cam";
const char fourty_two_hundred_wheel_friendly_name[] PROGMEM = "GM 4200 crank wheel";
const char thirty_six_minus_one_with_cam_fe3_friendly_name[] PROGMEM = "Mazda FE3 36-1 with cam";
const char six_g_seventy_two_with_cam_friendly_name[] PROGMEM = "Mitsubishi 6g72 with cam";
const char buell_oddfire_cam_friendly_name[] PROGMEM = "Buell Oddfire CAM wheel";
const char gm_ls1_crank_and_cam_friendly_name[] PROGMEM = "GM LS1 crank and cam";
const char gm_ls_58X_crank_and_4x_cam_friendly_name[] PROGMEM = "GM 58x crank and 4x cam";
const char lotus_thirty_six_minus_one_one_one_one_friendly_name[] PROGMEM = "Odd Lotus 36-1-1-1-1 flywheel";
const char honda_rc51_with_cam_friendly_name[] PROGMEM = "Honda RC51 with cam";
const char thirty_six_minus_one_with_second_trigger_friendly_name[] PROGMEM = "36-1 crank with 2nd trigger on teeth 33-34";
const char chrysler_ngc_thirty_six_plus_two_minus_two_with_ngc4_cam_friendly_name[] PROGMEM = "Chrysler NGC 36+2-2 crank, NGC 4-cyl cam";
const char chrysler_ngc_thirty_six_minus_two_plus_two_with_ngc6_cam_friendly_name[] PROGMEM = "Chrysler NGC 36-2+2 crank, NGC 6-cyl cam";
const char chrysler_ngc_thirty_six_minus_two_plus_two_with_ngc8_cam_friendly_name[] PROGMEM = "Chrysler NGC 36-2+2 crank, NGC 8-cyl cam";
const char weber_iaw_with_cam_friendly_name[] PROGMEM = "Weber-Marelli 8 crank+2 cam pattern";
const char fiat_one_point_eight_sixteen_valve_with_cam_friendly_name[] PROGMEM = "Fiat 1.8 16V crank and cam";
const char three_sixty_nissan_cas_friendly_name[] PROGMEM = "Nissan 360 CAS with 6 slots";
const char twenty_four_minus_two_with_second_trigger_friendly_name[] PROGMEM = "Mazda CAS 24-2 with single pulse outer ring";
const char yamaha_eight_tooth_with_cam_friendly_name[] PROGMEM = "Yamaha 2002-03 R1 8 even-tooth crank with 1 tooth cam";
const char gm_four_tooth_with_cam_friendly_name[] PROGMEM = "GM 4 even-tooth crank with 1 tooth cam";
const char gm_six_tooth_with_cam_friendly_name[] PROGMEM = "GM 6 even-tooth crank with 1 tooth cam";
const char gm_eight_tooth_with_cam_friendly_name[] PROGMEM = "GM 8 even-tooth crank with 1 tooth cam";
const char volvo_d12acd_with_cam_friendly_name[] PROGMEM = "Volvo d12[acd] crank with 7 tooth cam";
const char mazda_thirty_six_minus_two_two_two_with_six_tooth_cam_friendly_name[] PROGMEM = "Mazda 36-2-2-2 with 6 tooth cam";
const char mitsubishi_4g63_4_2_friendly_name[] PROGMEM = "Mitsubishi 4g63 aka 4/2 crank and cam";
const char audi_135_with_cam_friendly_name[] PROGMEM = "Audi 135 tooth crank and cam";
const char honda_d17_no_cam_friendly_name[] PROGMEM = "Honda D17 Crank (12+1)";
const char mazda_323_au_friendly_name[] PROGMEM = "Mazda 323 AU version";
const char daihatsu_3cyl_friendly_name[] PROGMEM = "Daihatsu 3+1 distributor (3 cylinders)";
const char miata_9905_friendly_name[] PROGMEM = "Miata 99-05";
const char twelve_with_cam_friendly_name[] PROGMEM = "12/1 (12 crank with cam)";
const char twenty_four_with_cam_friendly_name[] PROGMEM = "24/1 (24 crank with cam)";
const char subaru_six_seven_name_friendly_name[] PROGMEM = "Subaru 6/7 crank and cam";
const char gm_seven_x_friendly_name[] PROGMEM = "GM 7X";
const char four_twenty_a_friendly_name[] PROGMEM = "DSM 420a";
const char ford_st170_friendly_name[] PROGMEM = "Ford ST170";
const char mitsubishi_3A92_friendly_name[] PROGMEM = "Mitsubishi 3A92";
const char Toyota_4AGE_CAS_friendly_name[] PROGMEM = "Toyota 4AGE";
const char Toyota_4AGZE_friendly_name[] PROGMEM = "Toyota 4AGZE";
const char Suzuki_DRZ400_friendly_name[] PROGMEM = "Suzuki DRZ400";
const char Jeep_2000_4cyl_friendly_name[] PROGMEM = "Jeep 2000 4cyl";
const char Jeep_2000_6cyl_friendly_name[] PROGMEM = "Jeep 2000 6 cyl";
const char BMW_N20_friendly_name[] PROGMEM = "BMW N20";
const char VIPER9602_friendly_name[] PROGMEM = "Dodge Viper V10 1996-2002";
const char thirty_six_minus_two_with_second_trigger_friendly_name[] PROGMEM = "36-2 with 1 tooth cam";
const char GM_40_Tooth_Trans_OSS_friendly_name[] PROGMEM = "GM 40 tooth OSS wheel for Transmissions";

/* Very simple 50% duty cycle */
const unsigned char dizzy_four_cylinder[] PROGMEM =
    {
        /* dizzy 4 cylinder */
        2, 1, 0, 2, 0 /* two pulses per crank revolution (one per cylinder) */
};

/* Very simple 50% duty cycle */
const unsigned char dizzy_six_cylinder[] PROGMEM =
    {
        /* dizzy 6 cylinder */
        2, 1, 0, 3, 0 /* three pulses per crank revolution (one per cylinder) */
};

/* Very simple 50% duty cycle */
const unsigned char dizzy_eight_cylinder[] PROGMEM =
    {
        /* dizzy 8 cyl */
        2, 1, 0, 4, 0 /* four pulses per crank revolution (one per cylinder) */
};

/* Standard bosch 60-2 pattern, 50% duty cyctle during normal teeth */
const unsigned char sixty_minus_two[] PROGMEM =
    {/* 60-2 */
     2, 1, 0, 58, 1, 0, 4, 0};

/* Bosch 60-2 pattern with 2nd trigger on rotation 2,
 * 50% duty cyctle during normal teeth */
const unsigned char sixty_minus_two_with_cam[] PROGMEM =
    {/* 60-2 */
     2, 1, 0, 58, 1, 0, 4, 2, 1, 0, 35, 1, 1, 1, 1, 2, 1, 2, 1, 0, 22, 1, 0, 4, 0};

/* 60-2 pattern with half moon cam trigger (cam input is high for one rotation and low for second rotation),
 * 50% duty cyctle during normal teeth */
const unsigned char sixty_minus_two_with_halfmoon_cam[] PROGMEM =
    {/* 60-2 */
     2, 1, 0, 43, 2, 3, 2, 15, 1, 2, 4, 2, 3, 2, 43, 2, 1, 0, 15, 1, 0, 4, 0};

/* Standard ford/mazda and aftermarket 36-1 pattern, 50% duty cyctle during normal teeth */
const unsigned char thirty_six_minus_one[] PROGMEM =
    {/* 36-1 */
     2, 1, 0, 35, 1, 0, 2, 0};

/* Standard ford/mazda and aftermarket 36-1 pattern, 50% duty cyctle during normal teeth */
const unsigned char twenty_four_minus_one[] PROGMEM =
    {/* 36-1 */
     2, 1, 0, 23, 1, 0, 2, 0};

/* 4-1 crank signal 50% duty cycle with Cam tooth enabled during the second rotation prior to tooth 2 */
const unsigned char four_minus_one_with_cam[] PROGMEM =
    {/* 4-1 with cam */
     2, 0, 1, 3, 1, 0, 3, 1, 1, 1, 1, 2, 1, 2, 1, 0, 2, 1, 0, 1, 0};

/* Yamaha R6 crank trigger 8 teeth missing one, (22.5deg low, 22.5deg high) 50% duty cycle during normal teeth */
const unsigned char eight_minus_one[] PROGMEM =
    {/* 8-1 */
     2, 0, 1, 7, 1, 0, 2, 0};

/* 40deg low, 20 deg high per tooth, cam signal on second rotation during 40deg low portion of 3rd tooth */
const unsigned char six_minus_one_with_cam[] PROGMEM =
    {/* 6-1 with cam */
     3, 0, 0, 1, 5, 1, 0, 5, 1, 1, 1, 1, 0, 2, 1, 1, 1, 1, 2, 2, 3, 1, 0, 0, 3, 1, 0, 1, 0};

/* 25 deg low, 5 deg high, #12 is missing,  cam is high for 25 deg on second crank rotation just after tooth 21 (9) */
const unsigned char twelve_minus_one_with_cam[] PROGMEM =
    {/* 12-1 with cam */
     6, 0, 0, 0, 0, 0, 1, 11, 1, 0, 11, 6, 1, 0, 0, 0, 0, 0, 8, 1, 1, 1, 1, 2, 5, 6, 1, 0, 0, 0, 0, 0, 2, 1, 0, 1, 0};

/* Ford V10 version of EDIS with 40 teeth instead of 36, 50% duty cycle during normal teeth.. */
const unsigned char fourty_minus_one[] PROGMEM =
    {/* 40-1 */
     2, 0, 1, 39, 1, 0, 2, 0};

/* 50deg off, 40 deg on dissy style signal */
const unsigned char dizzy_four_trigger_return[] PROGMEM =
    {/* dizzy trigger return */
     1, 0, 5, 1, 1, 4, 0};

/* Oddfire V twin  135/225 split */
const unsigned char oddfire_vr[] PROGMEM =
    {/* Oddfire VR */
     9, 1, 0, 0, 0, 0, 0, 0, 0, 0, 2, 1, 0, 6, 0};

/* GM LT1 360 and 8 wheel, see http://powerefi.com/files/opti-LT1-count.JPG */
const unsigned char optispark_lt1[] PROGMEM =
    {/* Optispark 360 outside teeth, 8 varying inside teeth */
     /* 1   2   3   4   5   6   7   8   9   10  11  12  13  14  15  16  17  18  19  20  21  22  23  24  25  26  27  28  29  30 */
     2, 0, 1, 43, 2, 2, 3, 7, 2, 0, 1, 38, 2, 2, 3, 2, 2, 0, 1, 43, 2, 2, 3, 12, 2, 0, 1, 33, 2, 2, 3, 2, 2, 0, 1, 43, 2, 2, 3, 17, 2, 0, 1, 28, 2, 2, 3, 2, 2, 0, 1, 43, 2, 2, 3, 22, 2, 0, 1, 23, 2, 2, 3, 2, 0};

const unsigned char twelve_minus_three[] PROGMEM =
    {/* 12-3, http://www.msextra.com/doc/triggers/12_3_wheel_133.jpg */
     4, 1, 0, 0, 0, 9, 1, 0, 12, 0};

const unsigned char thirty_six_minus_two_two_two[] PROGMEM =
    {
        // H4 version
        2, 1, 0, 13, 1, 0, 4, 1, 1, 1, 1, 0, 5, 2, 1, 0, 13, 1, 0, 4, 2, 1, 0, 3, 0};

const unsigned char thirty_six_minus_two_two_two_h6[] PROGMEM =
    {
        // H6 version
        2, 1, 0, 19, 1, 0, 4, 2, 1, 0, 10, 1, 0, 4, 1, 1, 1, 1, 0, 5, 0};

const unsigned char thirty_six_minus_two_two_two_with_cam[] PROGMEM =
    {/* 36-2-2-2 H4 with cam  */
     1, 1, 1, 1, 0, 2, 1, 2, 1, 1, 0, 2, 1, 1, 1, 1, 0, 5, 2, 1, 0, 16, 1, 0, 1, 1, 2, 1, 1, 0, 2, 2, 1, 0, 13, 1, 0, 4, 1, 1, 1, 1, 0, 5, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 2, 1, 2, 1, 0, 14, 1, 0, 4, 2, 1, 0, 12, 0};

const unsigned char fourty_two_hundred_wheel[] PROGMEM =
    {/* 4200 wheel http://msextra.com/doc/triggers/4200_timing.pdf */
     /* 55 deg high, 5 deg low, 55 deg high, 5 deg low,
      * 5 deg high, 5 deg low, 45 deg high, 5 deg low,
      * 55 deg high, 5 deg low, 65 deg high, 5 deg low,
      * 45 deg high, 5 deg low, (360 degreees ) */
     1, 1, 11, 1, 0, 1, 1, 1, 11, 2, 0, 1, 2, 1, 1, 8, 12, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 1, 1, 2, 1, 0, 1, 1, 1, 9, 1, 0, 1, 0};

/* Mazda F3 36-1 with cam */
const unsigned char thirty_six_minus_one_with_cam_fe3[] PROGMEM =
    {/* 36-1 with cam, 3 cam teeth, 2 180deg from each other */
     2, 1, 0, 9, 1, 3, 1, 1, 2, 1, 1, 3, 1, 2, 0, 1, 24, 1, 0, 3, 2, 1, 0, 6, 6, 3, 2, 3, 0, 1, 0, 2, 2, 1, 0, 23, 1, 0, 2, 0};

/* Mitsubishi 6g72 crank/cam */
const unsigned char six_g_seventy_two_with_cam[] PROGMEM =
    {/* Mitsubishi 6g72 */
     /* Crank signal's are 50 deg wide, and one per cylinder
      * Cam signals have 3 40 deg wide teeh and one 85 deg wide tooth
      * Counting both From TDC#1
      * Crank: 40 deg high, 70 deg low (repeats whole cycle)
      * Cam: 70 deg high, 80 deg low, 40 deg high, 150 deg low,
      * 40 deg high, 130 deg low, 40 deg high, 155 deg low
      */
     1, 3, 9, 1, 2, 5, 1, 0, 9, 1, 1, 7, 1, 3, 3, 1, 2, 5, 1, 0, 9, 1, 1, 10, 1, 0, 11, 1, 2, 3, 1, 3, 5, 1, 1, 5, 1, 0, 14, 1, 1, 7, 1, 3, 3, 1, 2, 5, 1, 0, 9, 1, 1, 10, 1, 0, 12, 1, 2, 2, 1, 3, 1, 0};

const unsigned char buell_oddfire_cam[] PROGMEM =
    {/* Buell oddfire cam wheel */
     /* Wheel is a cam wheel (degress are in crank degrees
      * 36 deg high, 54 deg low,
      * 36 deg high, 54 deg low,
      * (Rear at TDC) 36 deg high,
      * 1889 deg low, 36 deg high
      * 54 deg low, 36 deg high,
      * 54 deg low, (Front at TDC),
      * 36 deg high, 99 deg low
      */
     10, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 3, 1, 0, 15, 10, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 3, 1, 0, 5, 0};

const unsigned char gm_ls1_crank_and_cam[] PROGMEM =
    {/* GM LS1 24 tooth crank snd 1 tooth cam */
     /* 12 deg low, 3 deg high, 3 deg low,
      * 12 deg high, 3deg low, 12 deg high,
      * 3 deg low, 12 deg high, 3 deg low,
      * 12 deg high, 3 deg low, 12 deg high,
      * 12 deg low, 3 deg high, 3 deg low,
      * 12 deg high, 3 deg low, 12 deg high,
      * 3 deg low, 12 deg high, 12 deg low,
      * 3 deg high, 12 deg low, 3 deg high,
      * 3 deg low, 12 deg high, 3 deg low,
      * 12 deg high, 12 deg low, 3 deg high,
      * 12 deg low, 3 deg high, 12 deg low,
      * 3 deg high, 12 deg low, 3 deg high,
      * 3 deg low, 12 deg high, 12 deg low,
      * 3 deg high, 3 deg low, 12 deg high,
      * 12 deg low, 3 deg high, 12 deg low,
      * 3 deg high, 12 deg low, 3 deg high
      * Second rotation is the SAME pattern
      * with cam signal held high for 360
      * crank degrees */
     1, 4, 1, 1, 0, 11, 15, 1, 1, 1, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 5, 1, 1, 3, 1, 0, 12, 15, 1, 1, 1, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 3, 15, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 15, 1, 1, 1, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 15, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3, 1, 1, 3, 1, 0, 3, 1, 1, 12, 1, 0, 12, 1, 1, 3, 1, 0, 3, 1, 1, 12, 1, 0, 12, 15, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3, 1, 1, 3, 1, 2, 12, 15, 3, 3, 3, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 3, 3, 5, 1, 3, 3, 1, 2, 12, 15, 3, 3, 3, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 15, 3, 3, 3, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 15, 3, 3, 3, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 3, 3, 2, 15, 3, 3, 3, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 3, 1, 3, 3, 1, 2, 3, 1, 3, 12, 1, 2, 12, 1, 3, 3, 1, 2, 3, 1, 3, 12, 1, 2, 12, 15, 3, 3, 3, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 3, 1, 3, 3, 0};

// Added by Dale Follett of Twisted Builds LLC for GM gen4 LS 58x 4x crank cam simulation.
const unsigned char GM_LS_58X_crank_and_4x_cam[] PROGMEM =
    { // 58x LS crank 4x LS cam
        2, 1, 0, 2, 2, 3, 2, 6, 2, 1, 0, 24, 2, 3, 2, 6, 2, 1, 0, 4, 2, 3, 2, 16, 1, 2, 4, 2, 3, 2, 8, 2, 1, 0, 4, 2, 3, 2, 26, 2, 1, 0, 20, 1, 0, 4, 0};

/* Lotus 36-1-1-1-1 wheel, missing teeth at
 * 14, 17, 32 and 36
 */
const unsigned char lotus_thirty_six_minus_one_one_one_one[] PROGMEM =
    {/* 36-1-1-1-1 */
     2, 1, 0, 13, 6, 0, 0, 1, 0, 1, 0, 2, 2, 1, 0, 12, 1, 0, 2, 2, 1, 0, 3, 1, 0, 2, 0};
const unsigned char honda_rc51_with_cam[] PROGMEM =
    {/* Honda RC51 oddfire 90deg Vtwin with cam */
     2, 0, 1, 5, 1, 0, 1, 1, 3, 1, 2, 0, 1, 9, 4, 0, 3, 0, 1, 2, 2, 0, 1, 5, 0};

/* 36-1 with second trigger pulse across teeth 33-34 on first rotation */
const unsigned char thirty_six_minus_one_with_second_trigger[] PROGMEM =
    {/* 36-1 */
     2, 1, 0, 32, 2, 3, 2, 2, 1, 1, 1, 1, 0, 3, 2, 1, 0, 35, 1, 0, 2, 0};

const unsigned char chrysler_ngc_thirty_six_plus_two_minus_two_with_ngc4_cam[] PROGMEM =
    {/* 36+2-2 NGC-4 needs 1 deg resolution, Chrysler NGC engines used in Chrysler/Dodge/Jeep
      * cam edges are at 26,62,98,134,170,314,350,368,422,458,494,530,674 and 710 dev
      * crank is 36 teeth with two sets of two missing teeth ~180 degrees apart
      * The sets of missing teeth have different polarity to distinguish position
      */
     /* Crankshaft degrees
      1   3   5   7   9  11  13  15  17  19  21  23  25  27  29  31  33  35  37  39  41 */
     1, 0, 5, 1, 1, 20, 10, 3, 3, 3, 3, 3, 2, 2, 2, 2, 2, 3, 1, 3, 5, 1, 2, 1, 10, 0, 0, 0, 0, 1, 1, 1, 1, 1, 0, 3, 1, 0, 4, 1, 1, 2, 10, 3, 3, 3, 2, 2, 2, 2, 2, 3, 3, 3, 1, 3, 3, 1, 2, 3, 10, 0, 0, 1, 1, 1, 1, 1, 0, 0, 0, 3, 1, 0, 2, 1, 1, 4, 1, 3, 1, 1, 2, 5, 1, 3, 5, 1, 2, 25, 10, 3, 3, 3, 3, 3, 2, 2, 2, 2, 2, 10, 1, 3, 5, 1, 2, 3, 10, 0, 0, 1, 1, 1, 1, 1, 0, 0, 0, 3, 1, 0, 2, 1, 1, 4, 10, 3, 2, 2, 2, 2, 2, 3, 3, 3, 3, 2, 1, 3, 16, 10, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 3, 1, 1, 5, 1, 0, 1, 10, 2, 2, 2, 2, 3, 3, 3, 3, 3, 2, 3, 1, 2, 4, 1, 3, 2, 10, 1, 1, 1, 0, 0, 0, 0, 0, 1, 1, 3, 1, 1, 3, 1, 0, 3, 10, 2, 2, 3, 3, 3, 3, 3, 2, 2, 2, 3, 1, 2, 2, 1, 3, 4, 1, 1, 1, 1, 0, 5, 1, 1, 5, 1, 0, 25, 10, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 10, 1, 1, 5, 1, 0, 3, 10, 2, 2, 3, 3, 3, 3, 3, 2, 2, 2, 3, 1, 2, 2, 1, 3, 4, 1, 1, 1, 1, 0, 5, 1, 1, 5, 0};

const unsigned char chrysler_ngc_thirty_six_minus_two_plus_two_with_ngc6_cam[] PROGMEM =
    {/*
     Crank is same as NGC 4 pattern except cylinder 1 TDC is 180 degrees off, hence 36-2+2

     Cam information has been determined from:
     https://rotkee.com/en/wavebase/good-timing-ckp-cmp-signal-dodge-charger-lx-ld-2006-2010?brand=167
     https://rotkee.com/en/wavebase/good-timing-ckp-cmp-signal-jeep-liberty-kj-2002-2007?brand=180
     https://rotkee.com/en/wavebase/good-timing-ckp-cmp-signal-dodge-caravan-2008-2020?brand=167
     Cam cylinder 1 has been determined from https://youtu.be/CVXKXmCudAs?t=893

     Cam has 6 groups consisting of 1-3 teeth. Each group starting 120 degrees apart.
     The number of teeth per group form the pattern 3-1-2-3-2-1 of which any 2 values can be used to determine position.
     Each tooth is ~10 degrees wide and is spaced ~21 degrees from the other teeth.
     */
     1, 2, 2, 1, 0, 11, 1, 2, 10, 1, 0, 2, 1, 1, 5, 1, 0, 4, 1, 2, 1, 1, 3, 5, 1, 2, 4, 10, 0, 1, 1, 1, 1, 1, 0, 0, 0, 0, 6, 1, 0, 1, 1, 1, 5, 1, 0, 2, 1, 2, 3, 1, 3, 5, 1, 2, 2, 10, 0, 0, 0, 1, 1, 1, 1, 1, 0, 0, 6, 1, 0, 3, 1, 1, 25, 1, 0, 5, 1, 1, 5, 1, 0, 5, 1, 1, 5, 1, 0, 2, 1, 2, 3, 1, 3, 5, 1, 2, 2, 1, 0, 3, 1, 1, 5, 1, 0, 3, 1, 2, 2, 1, 3, 5, 1, 2, 3, 10, 0, 0, 1, 1, 1, 1, 1, 0, 0, 0, 8, 1, 0, 2, 1, 1, 5, 1, 0, 2, 1, 2, 3, 1, 3, 5, 1, 2, 2, 1, 0, 11, 1, 2, 10, 1, 0, 2, 1, 1, 5, 1, 0, 4, 1, 2, 1, 1, 3, 5, 1, 2, 4, 10, 0, 1, 1, 1, 1, 1, 0, 0, 0, 0, 6, 1, 0, 1, 1, 1, 5, 1, 0, 2, 1, 2, 3, 1, 3, 5, 1, 2, 2, 1, 0, 3, 1, 1, 5, 1, 0, 3, 1, 2, 2, 1, 3, 5, 1, 2, 3, 10, 0, 0, 1, 1, 1, 1, 1, 0, 0, 0, 4, 1, 0, 2, 1, 1, 25, 1, 0, 5, 1, 1, 5, 1, 0, 5, 1, 1, 5, 1, 0, 2, 1, 2, 3, 1, 3, 5, 1, 2, 2, 10, 0, 0, 0, 1, 1, 1, 1, 1, 0, 0, 11, 1, 2, 3, 1, 3, 5, 0};

const unsigned char chrysler_ngc_thirty_six_minus_two_plus_two_with_ngc8_cam[] PROGMEM =
    {/*
     Crank is same as NGC 4 pattern except cylinder 1 TDC is 180 degrees off, hence 36-2+2

     Cam information has been determined from:
     https://rotkee.com/en/wavebase/good-timing-ckp-cmp-in-cylinder-pressure-dodge-ram-3-dr-dh-d1-dc-dm-2001-2009?brand=167
     https://rotkee.com/en/wavebase/good-timing-ckp-cmp-signal-dodge-durango-2-2003-2008?brand=167
     https://rotkee.com/en/wavebase/good-timing-ckp-cmp-signal-dodge-ram-3-dr-dh-d1-dc-dm-2001-2009?engine=2299

     Cam has 8 groups consisting of 1-3 teeth. Each group starting 90 degrees apart.
     The number of teeth per group form the pattern 1-2-3-2-2-1-3-1 of which any 2 values can be used to determine position.
     Each tooth is ~8 degrees wide and is spaced ~21 degrees from the other teeth.
     */
     1, 0, 25, 10, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 5, 1, 1, 2, 1, 3, 3, 1, 2, 5, 10, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 8, 1, 1, 2, 1, 3, 3, 1, 2, 5, 1, 1, 5, 1, 0, 5, 1, 1, 3, 1, 3, 8, 1, 1, 14, 10, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 4, 1, 0, 5, 1, 1, 2, 1, 3, 3, 1, 2, 5, 1, 1, 5, 1, 0, 5, 1, 1, 3, 1, 3, 2, 1, 2, 5, 1, 3, 1, 1, 1, 4, 1, 0, 5, 1, 1, 4, 1, 3, 1, 1, 2, 5, 1, 3, 2, 10, 1, 1, 1, 0, 0, 0, 0, 0, 1, 1, 4, 1, 3, 3, 1, 2, 5, 1, 1, 5, 1, 0, 8, 1, 2, 8, 1, 0, 9, 10, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 5, 1, 1, 2, 1, 3, 3, 1, 2, 5, 1, 1, 5, 1, 0, 5, 1, 1, 3, 1, 3, 2, 1, 2, 5, 1, 3, 1, 10, 1, 1, 1, 1, 0, 0, 0, 0, 0, 1, 6, 1, 1, 1, 1, 3, 3, 1, 2, 5, 1, 1, 5, 1, 0, 5, 1, 1, 25, 10, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 4, 1, 0, 5, 1, 1, 2, 1, 3, 3, 1, 2, 5, 1, 1, 5, 1, 0, 5, 1, 1, 3, 1, 3, 2, 1, 2, 5, 1, 3, 1, 1, 1, 4, 1, 0, 5, 1, 1, 4, 1, 3, 1, 1, 2, 5, 1, 3, 2, 10, 1, 1, 1, 0, 0, 0, 0, 0, 1, 1, 4, 1, 3, 3, 1, 2, 5, 1, 1, 5, 0};

const unsigned char weber_iaw_with_cam[] PROGMEM =
    {/*Weber marelli (Cosworth/Lancia) from jimstim
     80 deg low, 10 deg high, Tooth 1
     20 deg low, 45 deg cam pulse, 15 deg low, 10 deg high, Cam tooth 1 and crank tooth 2
     80 deg low, 10 deg high, Crank tooth 3
     20 deg low, 45 deg cam pulse, 15 deg low, 10 deg high, Cam tooth2 and crank tooth 4
     80 deg low, 10 deg high, Crank tooth 5
     80 deg low, 10 deg high  Crank tooth 6
     80 deg low, 10 deg high, Crank tooth 7
     80 deg low, 10 deg high  Crank tooth 8 */
     1, 0, 16, 1, 1, 2, 1, 0, 4, 1, 2, 4, 1, 0, 8, 1, 1, 2, 1, 0, 16, 1, 1, 2, 1, 0, 4, 1, 2, 4, 1, 0, 8, 1, 1, 2, 1, 0, 16, 1, 1, 2, 1, 0, 16, 1, 1, 2, 1, 0, 16, 1, 1, 2, 1, 0, 16, 1, 1, 2, 0};

const unsigned char fiat_one_point_eight_sixteen_valve_with_cam[] PROGMEM =
    {
        /* Starting from TDC #1
         * Cam is high for 40 deg, low for 20, high 170deg, low for 170, high for 20,
         * low for 170, then high for 130
         * Crank is low for 97, high for 5, low for 27, high for 5, low for 50, high for 5,
         * low for 102, high for 5, low for 27, high for 5, low for 50, high for 5,
         * low for 102, high for 5, low for 27, high for 5, low for 50, high for 5,
         * low for 102, high for 5, low for 27, high for 5, low for 50, high for 5, low for 5
         * http://msextra.com/doc/triggers/fiat1.8-16v.jpg
         */
        /* Crankshaft degrees
       1   3   5   7   9  11  13  15  17  19  21  23  25  27  29  31  33  35  37  39  */
        1, 2, 40, 1, 0, 20, 1, 2, 23, 1, 3, 5, 1, 2, 27, 1, 3, 5, 1, 2, 50, 1, 3, 5, 1, 2, 55, 1, 0, 33, 1, 1, 5, 1, 0, 27, 1, 1, 5, 1, 0, 50, 1, 1, 5, 1, 0, 45, 1, 2, 20, 1, 0, 23, 1, 1, 5, 1, 0, 27, 1, 1, 5, 1, 0, 50, 1, 1, 5, 1, 0, 55, 1, 2, 33, 1, 3, 5, 1, 2, 27, 1, 3, 5, 1, 2, 50, 1, 3, 5, 1, 2, 5, 0};

const unsigned char three_sixty_nissan_cas[] PROGMEM =
    /* This version has the 360 teeth on the cam
      {
        1,1,1,2,2,0,4,2,3,1,56,2,2,0,8,2,3,1,52,2,2,0,12,2,3,1,48,2,2,0,16,2,3,1,44,2,2,0,20,2,3,1,40,2,2,0,24,2,3,1,35,1,3,1,0
      };*/
    {
        /* Home teeth every 120 degrees in increasing widths (8,16,24,32,40,48) */
        /* Crankshaft degrees
        1   3   5   7   9  11  13  15  17  19  21  23  25  27  29  31  33  35  37  39  */
        1, 2, 1, 2, 1, 0, 4, 2, 3, 2, 56, 2, 1, 0, 8, 2, 3, 2, 52, 2, 1, 0, 12, 2, 3, 2, 48, 2, 1, 0, 16, 2, 3, 2, 44, 2, 1, 0, 20, 2, 3, 2, 40, 2, 1, 0, 24, 2, 3, 2, 35, 1, 3, 1, 0};

const unsigned char twenty_four_minus_two_with_second_trigger[] PROGMEM =
    {
        /* See http://postimg.org/image/pcwkrxktx/, 24-2 inner ring, single outer pulse */
        3, 1, 0, 0, 10, 1, 3, 1, 1, 2, 5, 1, 3, 1, 3, 0, 0, 1, 10, 1, 0, 5, 0};

/* eight tooth with 1 tooth cam */
const unsigned char yamaha_eight_tooth_with_cam[] PROGMEM =
    {/* Yamaha R1 (02-03) 8 tooth crank with 1 tooth cam */
     4, 0, 0, 0, 1, 8, 1, 0, 1, 1, 2, 2, 1, 3, 1, 1, 2, 1, 4, 0, 0, 1, 0, 6, 1, 0, 2, 1, 1, 1, 0};

/* 50% dutycle, 4 tooth + 1 cam */
const unsigned char gm_four_tooth_with_cam[] PROGMEM =
    {
        /* 4 cylinder with 1 cam pulse for 360 crank degrees */
        2, 1, 0, 2, 2, 3, 2, 2, 0 /* two pulses per crank revolution (one per cylinder) */
};

/* 50% dutycle, 6 tooth + 1 cam */
const unsigned char gm_six_tooth_with_cam[] PROGMEM =
    {
        /* 6 cylinder with 1 cam pulse for 360 crank degrees */
        2, 1, 0, 3, 2, 3, 2, 3, 0 /* three pulses per crank revolution (one per cylinder) */
};

/*  50% dutycle, 8 tooth + 1 cam */
const unsigned char gm_eight_tooth_with_cam[] PROGMEM =
    {
        /* 8 cylinder with 1 cam pulse for 360 crank degrees  */
        2, 1, 0, 4, 2, 3, 2, 4, 0 /* four pulses per crank revolution (one per cylinder) */
};

const unsigned char volvo_d12acd_with_cam[] PROGMEM =
    {/* Volvo 6 cylinder dieslet  17-1-17-1-17-1 (60 overall teeth) */
     4, 0, 1, 1, 1, 12, 1, 2, 1, 4, 1, 1, 1, 0, 4, 1, 1, 3, 1, 2, 1, 1, 1, 11, 4, 0, 1, 1, 1, 17, 1, 2, 1, 1, 1, 11, 4, 0, 1, 1, 1, 17, 1, 2, 1, 1, 1, 11, 4, 0, 1, 1, 1, 17, 1, 2, 1, 1, 1, 11, 4, 0, 1, 1, 1, 17, 1, 2, 1, 1, 1, 11, 4, 0, 1, 1, 1, 17, 1, 2, 1, 1, 1, 11, 0};
const unsigned char mazda_thirty_six_minus_two_two_two_with_six_tooth_cam[] PROGMEM =
    {/* Mazda 36-2-2-2 with 6 tooth cam */
     5, 1, 1, 0, 0, 0, 7, 1, 1, 1, 5, 3, 2, 2, 2, 3, 2, 1, 1, 1, 1, 0, 3, 1, 1, 2, 1, 0, 13, 1, 1, 1, 1, 3, 1, 1, 2, 9, 1, 0, 4, 5, 1, 1, 0, 0, 0, 9, 1, 1, 1, 5, 3, 2, 2, 2, 3, 2, 1, 1, 1, 1, 0, 3, 1, 1, 2, 1, 0, 13, 1, 1, 1, 5, 3, 2, 2, 2, 3, 2, 5, 1, 0, 0, 0, 1, 13, 1, 1, 1, 1, 0, 13, 1, 1, 1, 1, 3, 1, 1, 2, 9, 1, 0, 4, 5, 1, 1, 0, 0, 0, 13, 1, 0, 10, 1, 1, 1, 5, 3, 2, 2, 2, 3, 2, 5, 1, 0, 0, 0, 1, 2, 1, 1, 1, 1, 0, 3, 0};

/* Mitsubish 4g63 aka 4/2 crank and cam */
const unsigned char mitsubishi_4g63_4_2[] PROGMEM =
    { // Split into 5 degree blocks (12 per line)
        1, 2, 11, 1, 0, 10, 1, 1, 14, 1, 0, 19, 1, 2, 3, 1, 3, 11, 1, 1, 3, 1, 0, 22, 1, 1, 14, 1, 0, 21, 1, 2, 1, 1, 3, 14, 1, 2, 1, 0};

/* Mitsubish 4g63 aka 4/2 crank and cam */
const unsigned char audi_135_with_cam[] PROGMEM =
    {
        4, 3, 3, 2, 2, 2, 1, 3, 2, 1, 2, 1, 4, 0, 1, 1, 0, 255, 4, 0, 1, 1, 0, 12, 1, 2, 1, 0};

/* Honda D17 12+1. 5 degree per entry*/
const unsigned char honda_d17_no_cam[] PROGMEM =
    {
        6, 1, 0, 0, 0, 0, 0, 11, 2, 1, 0, 2, 6, 0, 0, 1, 0, 0, 0, 11, 1, 0, 2, 2, 1, 0, 2, 1, 0, 2, 0};

/*
 * http://imgur.com/a/ynLWp
 */
const unsigned char mazda_323_au[] PROGMEM =
    {
        1, 0, 5, 1, 2, 1, 6, 0, 0, 1, 0, 0, 0, 2, 1, 0, 2, 1, 2, 1, 1, 0, 1, 1, 2, 1, 1, 1, 1, 1, 0, 5, 1, 1, 1, 0};

/*
 * http://www.msextra.com/doc/triggers/daihatsu-trigs.txt
 * http://www.msextra.com/doc/triggers/daihatsu3cyla.jpg
 * http://jbperf.com/JimStim/wheels_default.jsw
 * 5 degree per entry
 *
 */
const unsigned char daihatsu_3cyl[] PROGMEM =
    {
        6, 1, 1, 0, 0, 0, 0, 2, 1, 0, 36, 1, 1, 2, 1, 0, 46, 1, 1, 2, 1, 0, 46, 0};

/* Mitsubish 4g63 aka 4/2 crank and cam */
const unsigned char miata_9905[] PROGMEM =
    {
        1, 0, 6, 1, 2, 2, 1, 0, 11, 14, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 1, 0, 8, 1, 1, 2, 1, 0, 12, 1, 1, 2, 1, 0, 3, 4, 2, 2, 0, 0, 2, 1, 0, 9, 14, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 1, 0, 8, 1, 1, 2, 1, 0, 12, 1, 1, 2, 1, 0, 1, 0};

/* 25 deg low, 5 deg high, #12 is missing,  cam is high for 25 deg on second crank rotation just after tooth 21 (9) */
const unsigned char twelve_with_cam[] PROGMEM =
    {
        6, 0, 0, 0, 0, 0, 1, 21, 1, 2, 5, 6, 1, 0, 0, 0, 0, 0, 2, 1, 1, 1, 0};

/* 25 deg low, 5 deg high, #12 is missing,  cam is high for 25 deg on second crank rotation just after tooth 21 (9) */
const unsigned char twenty_four_with_cam[] PROGMEM =
    {
        3, 0, 0, 1, 42, 1, 2, 2, 1, 3, 1, 1, 2, 2, 3, 1, 0, 0, 4, 1, 1, 1, 0};

const unsigned char subaru_six_seven[] PROGMEM =
    {
        1, 0, 5, 6, 2, 2, 2, 0, 0, 0, 3, 1, 0, 60, 1, 1, 3, 1, 0, 29, 1, 1, 3, 1, 0, 52, 1, 1, 3, 1, 0, 27, 1, 2, 3, 1, 0, 60, 1, 1, 3, 1, 0, 29, 1, 1, 3, 1, 0, 52, 1, 1, 3, 1, 0, 27, 6, 2, 2, 2, 0, 0, 0, 2, 1, 0, 51, 1, 1, 3, 1, 0, 29, 1, 1, 3, 1, 0, 52, 1, 1, 3, 1, 0, 27, 1, 2, 3, 1, 0, 60, 1, 1, 3, 1, 0, 29, 1, 1, 3, 1, 0, 52, 1, 1, 3, 1, 0, 7, 0};

/* GM 7X for 6 cylinder engines */
/* https://speeduino.com/forum/download/file.php?id=4743 */
const unsigned char gm_seven_x[] PROGMEM =
    {
        1, 0, 21, 1, 1, 2, 1, 0, 28, 5, 1, 1, 0, 0, 0, 2, 1, 0, 20, 1, 1, 2, 1, 0, 28, 1, 1, 2, 1, 0, 28, 1, 1, 2, 1, 0, 28, 1, 1, 2, 1, 0, 7, 0};

/* DSM 420a Eclipse */
/* https://github.com/noisymime/speeduino/issues/133 */
const unsigned char four_twenty_a[] PROGMEM =
    {
        1, 0, 11, 1, 2, 10, 4, 3, 3, 2, 2, 4, 1, 2, 4, 1, 0, 6, 1, 1, 12, 4, 0, 0, 1, 1, 3, 1, 0, 12, 1, 2, 10, 4, 3, 3, 2, 2, 4, 1, 2, 10, 1, 3, 6, 1, 1, 6, 4, 0, 0, 1, 1, 3, 1, 0, 1, 0};

const unsigned char ford_st170[] PROGMEM =
    {
        1, 0, 1, 1, 1, 4, 1, 0, 13, 1, 1, 7, 1, 0, 5, 1, 1, 5, 1, 0, 5, 1, 1, 5, 1, 0, 4, 1, 1, 5, 1, 0, 5, 1, 1, 5, 1, 0, 5, 10, 3, 3, 3, 3, 3, 2, 2, 2, 2, 2, 3, 1, 3, 4, 10, 1, 0, 0, 0, 0, 0, 1, 1, 1, 1, 6, 10, 3, 2, 2, 2, 2, 2, 3, 3, 3, 3, 2, 1, 3, 1, 1, 2, 5, 1, 3, 3, 10, 1, 1, 0, 0, 0, 0, 0, 1, 1, 1, 6, 10, 3, 3, 2, 2, 2, 2, 2, 3, 3, 3, 3, 10, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 2, 10, 1, 1, 1, 0, 0, 0, 0, 0, 1, 1, 6, 1, 1, 3, 1, 0, 14, 1, 1, 6, 10, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 4, 1, 0, 5, 10, 3, 3, 3, 3, 3, 2, 2, 2, 2, 2, 3, 1, 3, 4, 10, 1, 0, 0, 0, 0, 0, 1, 1, 1, 1, 3, 1, 1, 2, 1, 0, 4, 1, 1, 6, 10, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 10, 1, 0, 5, 1, 1, 1, 1, 3, 4, 1, 2, 5, 1, 3, 5, 1, 2, 5, 1, 3, 6, 1, 2, 4, 1, 3, 3, 10, 1, 1, 1, 0, 0, 0, 0, 0, 1, 1, 7, 1, 1, 3, 1, 0, 3, 0};

const unsigned char mitsubishi_3A92[] PROGMEM =
    {
        2, 1, 0, 10, 1, 2, 1, 2, 0, 1, 11, 1, 0, 3, 2, 1, 0, 11, 2, 2, 0, 2, 2, 1, 0, 10, 1, 0, 2, 2, 1, 0, 11, 1, 2, 1, 2, 0, 1, 11, 1, 0, 5, 0};

/* 4AGE CAS inside dizzy, 4 pulses 2 per crank revolution one cam pulse at 5 Deg  */

const unsigned char toyota_4AGE_CAS[] PROGMEM =
    {
        1, 1, 2, 1, 2, 2, 1, 0, 32, 1, 1, 2, 1, 0, 34, 1, 1, 2, 1, 0, 34, 1, 1, 2, 1, 0, 34, 0};
/* 4AGZE inside dizzy, 24 pulses 12 per crank revolution one cam pulse at 5 Deg  */

const unsigned char toyota_4AGZE[] PROGMEM =
    {
        1, 1, 2, 1, 2, 1, 12, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 11, 1, 0, 9, 0};

const unsigned char suzuki_DRZ400[] PROGMEM =
    {
        1, 1, 6, 1, 2, 4, 1, 0, 2, 1, 3, 6, 1, 2, 2, 12, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 0, 0, 4, 1, 0, 4, 0};

const unsigned char jeep_2000_4cyl[] PROGMEM =
    {
        1, 0, 56, 10, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4, 1, 0, 21, 1, 2, 29, 10, 3, 2, 2, 2, 2, 2, 2, 2, 2, 2, 4, 1, 2, 50, 10, 3, 2, 2, 2, 2, 2, 2, 2, 2, 2, 4, 1, 2, 21, 1, 0, 29, 10, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3, 1, 1, 1, 1, 0, 3, 0};

const unsigned char jeep_2000_6cyl[] PROGMEM =
    {
        1, 0, 27, 10, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4, 1, 0, 6, 1, 2, 14, 10, 3, 2, 2, 2, 2, 2, 2, 2, 2, 2, 4, 1, 2, 20, 10, 3, 2, 2, 2, 2, 2, 2, 2, 2, 2, 4, 1, 2, 20, 10, 3, 2, 2, 2, 2, 2, 2, 2, 2, 2, 4, 1, 2, 6, 1, 0, 14, 10, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4, 1, 0, 20, 10, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3, 1, 1, 1, 1, 0, 2, 0};

const unsigned char bmw_n20[] PROGMEM =
    {
        2, 1, 0, 15, 2, 7, 6, 8, 2, 1, 0, 22, 2, 7, 6, 13, 1, 6, 4, 2, 7, 6, 15, 2, 1, 0, 7, 2, 7, 6, 23, 2, 1, 0, 13, 1, 0, 4, 0};

const unsigned char viper9602wheel[] PROGMEM =
    // Viper pattern has 10 total crank teeth that are on shortly in pairs. Cam is high for 360* of crank then low for the next 360* of crank
    // This pattern was added by Dale Follett of Twisted Builds LLC going off a supplied oscilloscope image of the wheel pattern. Due to this
    // There is no guarentees on this wheel pattern as of 3/24/2024 and this pattern should be used at your own risk. However it should be correct.
    // I'm basing this using percentages. 120 total "edges" but should duplicate the factory wheels. Will test with o-scope.
    {
        1, 2, 6, 6, 3, 3, 2, 2, 2, 2, 2, 1, 2, 12, 6, 3, 3, 2, 2, 2, 2, 2, 1, 2, 12, 6, 3, 3, 2, 2, 2, 2, 2, 1, 2, 12, 6, 3, 3, 2, 2, 2, 2, 2, 1, 2, 12, 6, 3, 3, 2, 2, 2, 2, 2, 1, 2, 6, 1, 0, 6, 6, 1, 1, 0, 0, 0, 0, 2, 1, 0, 12, 6, 1, 1, 0, 0, 0, 0, 2, 1, 0, 12, 6, 1, 1, 0, 0, 0, 0, 2, 1, 0, 12, 6, 1, 1, 0, 0, 0, 0, 2, 1, 0, 12, 6, 1, 1, 0, 0, 0, 0, 2, 1, 0, 6, 0};

/* 36-2 with second trigger pulse across teeth 33-34 on first rotation */
const unsigned char thirty_six_minus_two_with_second_trigger[] PROGMEM =
    {
        2, 1, 0, 7, 2, 3, 2, 2, 2, 1, 0, 25, 1, 0, 4, 2, 1, 0, 34, 1, 0, 4, 0};

// GM 40 tooth OSS wheel for transmission simulation. Simple on/off 40 teeth for 360* of rotation with no missing teeth.
// Added by Dale Follett of Twisted Builds LLC 02-23-2025 for transmission controller simulation.
const unsigned char GM40toothOSS[] PROGMEM =
    {
        2, 1, 0, 40, 0};

#endif
