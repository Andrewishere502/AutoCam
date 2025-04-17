'''This script consolidates image data from each AutoCamRun folder into
one data folder with all images and a meta data CSV file tracking each
image's origin folder (i.e. which "run" it came from, e.g. AutoCamRun3) and original file name within that run.
'''


import pandas as pd

import argparse
import pathlib
from collections import OrderedDict


def save_df_csv(df: pd.DataFrame, path: pathlib.Path) -> None:
    '''Save a given DataFrame object as a CSV file at the desired path.
    
    Arguments:
    df -- dataframe to save
    path -- path to save dataframe to
    '''
    df.to_csv(path_or_buf=path)
    return


def add_column(df: pd.DataFrame, new_column_name: str) -> pd.DataFrame:
    '''Add a column to a dataframe. Return the dataframe with the new
    column, but no values in this column for any row. Raise ValueError
    if a column with the desired new column's name already exists.
    '''
    if new_column_name in df.columns:
        raise ValueError(f'Cannot add duplicate column {new_column_name} to dataframe')
    return pd.concat([df, pd.DataFrame(columns=[new_column_name])], axis=0)


INDEX_COL = 'ID'    # Unique identifier for each image
# Define a list of columns that should be in the metadata file
COLUMNS = [
    'OriginDir',    # Name of the directory the image was in before processing
    'OriginName',   # Name of the image before processing
    'NewDir',       # Name of the directory the processed image is placed into
    'NewName',      # New name for the image after processing
    'NShrimp',      # Number of shrimp in the image
    'TankID',       # Which tank the image came from (1, 2, 3)
    'Date',         # Date (approximate) that the image was captured (mm/dd/yyyy)
    'Bubbles',      # If there are bubbles in the image or not (0/1)
    'Filter',       # If the filter is visible in the image (0/1)
]


# Parse command line arguments
parser = argparse.ArgumentParser()

# Add positional (mandatory) arguments
parser.add_argument('origin_dirs',
                    nargs='+',  # Require one or more values
                    type=pathlib.Path,
                    help='List of origin directories separated by a space'
                    )
parser.add_argument('new_dir',
                    type=pathlib.Path,
                    help='Output directory in which to consolidate images'
                    )

# Add optional arguments
parser.add_argument('-t', '--tank_id',
                    default=False,  # If not specified, will be False
                    help='Tank ID to use for all images'
                    )
parser.add_argument('-f', '--filter',
                    action='store_true',  # If not specified, will be False, otherwise true
                    help='If the filter can be seen in any of the images being consolidated'
                    )

# Parse the command line arguments
args = parser.parse_args()


# Create the data folder if it doesn't exist
if not args.new_dir.exists():
    args.new_dir.mkdir()


# Construct path to metadata CSV file
meta_file = args.new_dir / 'metadata.csv'
# Create the meta file with an appropriate header if it doesn't already
# exist.
if not meta_file.exists():
    print(f'Creating metadata.csv file.')
    with open(meta_file, 'w') as file:  # Create metadata file
        header = INDEX_COL + ',' + ','.join(COLUMNS)
        file.write(header + '\n')  # Add in the header


# Load the data, using the first column (column index 0) as the index
# column of the dataset
meta_df = pd.read_csv(meta_file, index_col=INDEX_COL)
header = meta_df.columns  # Get the columns in the DataFrame


# Verify all required columns exist within the dataframe.
# If a column does not exist, add it and save the dataframe.
for column in COLUMNS:
    if column not in header:
        meta_df = add_column(meta_df, column)
        save_df_csv(meta_df, meta_file)
        print(f'Added new column {column} to {meta_file.name}')


# Take each image from its directory, record its metadata in
# metadata.csv, and move it to the data directory
for origin_dir in args.origin_dirs:
    if not origin_dir.exists():
        print(f'Skipping {origin_dir}: Unable to locate directory')
        continue

    if not origin_dir.is_dir():
        print(f'Skipping {origin_dir}: Not a directory')
        continue

    # Make a copy of header as an ordered dictionary to preserve
    # order of column names.
    row = OrderedDict([(h, None) for h in header])

    # Set values that will be the same for all rows in this directory
    row['OriginDir'] = origin_dir
    row['NewDir'] = args.new_dir
    if args.tank_id:  # Set tank ID, if specified
        row['TankID'] = args.tank_id

    for img_orgin_name in origin_dir.iterdir():
        # Include only the file name, not its path
        row['OriginName'] = img_orgin_name.name

        # Do not give images #0: VSCode won't open 00000000.JPG
        im_num = len(meta_df) + 1
        img_new_name = pathlib.Path(f'{im_num:08d}' + img_orgin_name.suffix)
        row['NewName'] = img_new_name

        # Move the image to the consolidation directory
        img_orgin_name.rename(args.new_dir / img_new_name)

        # Add the row for this image into the meta DataFrame
        meta_df.loc[len(meta_df)] = row.values()

        # Save the meta data DataFrame as a CSV, overwriting the
        # old CSV.
        # Yes this is inefficient, but it ensures every image has
        # its meta data saved in the event of an error.
        save_df_csv(meta_df, meta_file)
