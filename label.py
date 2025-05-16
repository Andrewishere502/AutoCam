'''This script is to help with labeling shrimp in the images collected
from AutoCam.
'''

import argparse
from collections import deque
from typing import Deque, Any, Tuple, Union
import pathlib

import matplotlib.pyplot as plt
import pandas as pd


def save_df_csv(df: pd.DataFrame, path: pathlib.Path) -> None:
    '''Save a given DataFrame object as a CSV file at the desired path.
    
    Arguments:
    df -- dataframe to save
    path -- path to save dataframe to
    '''
    df.to_csv(path_or_buf=path)
    return


def add_to_label_queue(label_queue: Deque[Tuple[int, pathlib.Path]], df: pd.DataFrame, fetch_n: Union[int, None]=None) -> None:
    '''Append a specified number of images (i.e. path to the images)
    that need labeling to the labeling queue.
    
    Arguments:
    label_queue -- Queue to add image paths to
    df -- Dataframe containing image meta data, used to
          determine which images need labeling
    fetch_n -- Max number of images to add to the queue.
               If -1, add *all* images that need labeling
    '''
    n = 0
    for row in df.itertuples():
        # Get the index of the row within the dataframe
        index = row[0]
        # Label the row data with columns for more robust selection
        # of column values. Skip the first element in row since it is
        # the index.
        labeled_row_data = dict(zip(list(df.columns), row[1:]))
        # Check if any values for this image's metadata are missing,
        # indicating it hasn't been labeled yet.
        if pd.isna(labeled_row_data['NShrimp']):
            # Add the image path to the queue
            full_img_path = pathlib.Path(labeled_row_data['NewDir'], labeled_row_data['NewName'])
            label_queue.append((index, full_img_path))  # Add to right end
            # Will never == None, so won't exit loop early if
            # fetch_n == None
            n += 1
            if n == fetch_n:
                break
    return


def update_column(meta_df: pd.DataFrame, index: Any, column: str, value: Any) -> None:
    '''Add some value to a specific row and column in the dataframe.
    Modifies the dataframe in place.

    Arguments:
    meta_df -- dataframe to update
    index -- index in dataframe, row to update
    column -- column in dataframe to update
    value -- Set the value at row, column, to this
    '''
    # Set the number of shrimp for the image at the specified index
    meta_df.at[index, column] = value
    return


# Parse command line arguments
parser = argparse.ArgumentParser()

# Add positional (mandatory) arguments
parser.add_argument('data_dir',
                    type=pathlib.Path,
                    help='List of origin directories separated by a space'
                    )

# Add optional arguments
parser.add_argument('-n', '--fetch_n',
                    type=int,
                    help='Number of images to load into labeling queue'
                    )

# Parse the command line arguments
args = parser.parse_args()

meta_file = args.data_dir / 'metadata.csv'
meta_df = pd.read_csv(meta_file, index_col='ID')

# Create a queue and add some images to it. If fetch_n was specified
# load that number of images only. Otherwise, load in all unlabeled
# images.
label_queue: Deque[Tuple[int, pathlib.Path]] = deque()
add_to_label_queue(label_queue, meta_df, fetch_n=args.fetch_n)

# Continue labeling while the queue is not empty
while len(label_queue) > 0:
    # Load next image, this does not modify metadata.csv.
    img_index, img_path = label_queue.popleft()  # Pop from left end
    img_array = plt.imread(img_path)

    # Display image
    fig, ax = plt.subplots()
    # Configure figure title and the x/y axis
    ax.set_title(str(img_path))
    ax.set_xticks([])
    ax.set_yticks([])
    # Provide instructions on controls
    ax.set_xlabel('Click on each individual shrimp once.' \
                  '\nRight click to delete last point.' \
                  '\nPress enter when done.'
                 )
    ax.imshow(img_array)
    fig.tight_layout()  # Reduce margin around image
    plt.draw()  # Doesn't clear the fig or ax

    # Get points (from user clicking) of shrimp within image
    counting = True
    while counting:
        # Get points from the user until manually terminated
        shrimp_pts = fig.ginput(-1, timeout=-1)
        # Update the dataframe, but not the csv
        update_column(meta_df, img_index, 'NShrimp', len(shrimp_pts))
        # Update the csv
        save_df_csv(meta_df, meta_file)
        # Clear fig and ax to save memory
        plt.clf()
        plt.cla()
        plt.close()
        # Exit counting loop
        counting = False
