import boto3
from botocore.exceptions import NoCredentialsError


def upload_to_aws(local_file, bucket, s3_file):
    s3 = boto3.client("s3")

    try:
        s3.upload_file(local_file, bucket, s3_file)
        print(f"Upload Successful to {bucket}/{s3_file}")
        return True
    except FileNotFoundError:
        print(f"The file {local_file} was not found.")
        return False
    except NoCredentialsError:
        print("Credentials not available.")
        return False


# Define the parameters
local_file = "version-beta.txt"
bucket_name = "atarist.sidecart.xyz"
s3_file_name = "beta.txt"

# Upload the file
upload_to_aws(local_file, bucket_name, s3_file_name)
