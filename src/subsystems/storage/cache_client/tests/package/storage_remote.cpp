#include <RemoteClientProvider.h>

int main()
{
    const auto get_remote_client = &RemoteClientProvider::GetRemoteClient;
    return get_remote_client == nullptr ? 1 : 0;
}
