# Wave Record Splitter and Analyzer

This is a tool that can help you to cut large recordings with many songs into smaller files.
It automates the process of finding the start and end of each song and splitting the recording then.

This can be useful for processing large amounts of songs recorded from video game consoles and MIDI modules.

## General usage notes

- DAWs like REAPER split their recordings to avoid hitting the 4 GB size limit of single WAV files.
  The tool supports specifying multiple WAV files to be loaded as a single "recording" by:

  - specifying `--file` multiple times
  - passing a text file that lists all WAV files (one file per line) using the `--list` parameter

- The tool has multiple modes:

  - `ampstat` - output amplitude statistics, for calibration
  - `detect` - detect split points and generate a text file of them
  - `split` - split recording into multiple files, with applying optional gain

  The three modes are usually used in the order above.

## Calibration

1. run `wavrec-split ampstat` on sections of the recording that contain silence.
   (i.e. only noise floor)
2. Of the resulting output, look at the `smplMin` and `smplMax` values and take the highest value.
   (i.e. for -87 db, -85 db, -84 db, choose `-84`)
3. Add 2 to 3 db to the value to give it some additional tolerance.
   e.g. if the maximum is -84.2 db, use `-82`  
   → This value will be the "split amplitude" for the `--amp-split` parameter.
4. subtract 4 db (e.g. -86 db) to get the "finetuning amplitude" (`--amp-finetune` parameter)  
   The value should be slightly above the average noise floor.
   You may need to increase or decrease the value slightly in order to improve the trimming at the point where the sound fades out.

## Generating the trim point list

1. Create a text file that lists the destination WAV file names for all songs that are to be extracted from the recording.
2. run `wavrec-split detect -l "recording.txt" -s "names.txt" -a <split-amp> -A <finetune-amp>`  
   [optional] Pipe the output into a separate text file.
3. It will at first do a coarse scan print a list of songs and timestamps.
   This is mostly for information, as it makes it easier to notice false detections.
   If it generates too many false-positives or fading tones are cut off too quickly, you need to adjust the ´amp` levels.
4. The start/end points from the coarse scan are then fine-tuned and a list is generated of the following format:

   ```
   gain start-sample end-sample file-name-1
   gain start-sample end-sample file-name-2
   ...
   ```

   The "gain" (value in db) is the maximum volume boost you can apply to the song without clipping.
   (Except that rounding the value to 3 decimal digits can cause micro-clipping later.)

   This list is what you will use with the `split` command.

## Splitting the recording into multiple songs

1. Use the list from the previous step to split the recording:  
   `wavrec-split split -l "recording.txt" -t "trim-list.txt" -o "output-path"`
2. Wait for it to finish.

Notes:

- By default no extra silence is added.
  The songs immediately begin at second 0, which causes some players to skip the first note.  
  If you want to add silence, use the `--begin-silence` and `--end-silence` parameters.
- The standard configuration does NOT apply any volume gain.
  You need to enable that explicitly using the `--apply-gain` flag.

## Technical details

### Song start/end detection

The detection is performed in two steps.

1. coarse silence detection
2. fine-tuning of start/end points

#### Coarse silence detection

"silence" is detected by the wave form being quieter than the threshold (`amp-split`) for a certain amount of time.

During the coarse detection, a song begins immediately when the threshold is passed.
A song ends with the begining of a block of "silence".

However in order to reduce the amount of false positives, surpassing the threshold for less than 10 samples does NOT result in a new song.
This instead generates the `Outlier at X` message.

#### Start Point Fine-tuning

1. begin at start point from coarse scan
2. take note of the sign (positive/negative) of the sample value at the start point
3. search backwards until a sample value is reached that fullfills both of these conditions:

   - has the opposite sign of the sample at the coarse start point
   - is louder than (`amp-fine` - 12 db)

4. finally search forward until the sign of the waveform changes again
   (and thus matches the sign of the coarse start point sample)

The purpose of the start point fine-tuning is to capture the full "swing-in" curve of the first tone.

#### End Point Fine-tuning

1. begin at (end point from coarse scan - 100 ms)
2. for each block of 100 ms, calculate the average amplitude of all samples
3. When a block's average amplitude is larger than the one of the previous block:
   Stop: The fine-tuned end point is the beginning of this block.
4. Else continue searching for up to 4 seconds.
