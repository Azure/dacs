// Copyright (c) Microsoft Corporation.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

package eventhub

import (
	"context"
	"encoding/json"
	"fmt"

	"github.com/Azure/azure-sdk-for-go/sdk/azidentity"
	"github.com/Azure/azure-sdk-for-go/sdk/messaging/azeventhubs/v2"

	"github.com/Azure/dacs/test/pkg/logger"
)

var log = logger.New()

// SendEvent generates a credential and sends an event defined as a string
// map to the specified Azure Event Hub.
func SendEvent(ctx context.Context, eventHubNamespace string, eventHubName string, event map[string]any) error {

	cred, err := azidentity.NewDefaultAzureCredential(nil)
	if err != nil {
		return fmt.Errorf("failed to get managed identity credential: %v", err.Error())
	}

	client, err := azeventhubs.NewProducerClient(eventHubNamespace, eventHubName, cred, nil)
	if err != nil {
		return fmt.Errorf("failed to create new event hub producer: %v", err.Error())
	}
	defer func() { _ = client.Close(ctx) }()

	eventBytes, err := json.Marshal(event)
	if err != nil {
		return fmt.Errorf("failed to marshall event to bytes: %v", err.Error())
	}
	log.Info("Created EventHub event", "event", string(eventBytes))

	eventData := &azeventhubs.EventData{
		Body: eventBytes,
	}

	batch, err := client.NewEventDataBatch(ctx, nil)
	if err != nil {
		return fmt.Errorf("failed to create event data batch: %v", err.Error())
	}
	err = batch.AddEventData(eventData, nil)
	if err != nil {
		return fmt.Errorf("failed to add event data to batch: %v", err.Error())
	}

	return client.SendEventDataBatch(ctx, batch, nil)
}
