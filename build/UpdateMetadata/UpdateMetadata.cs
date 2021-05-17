// ------------------------------------------------------------
// Copyright (c) Microsoft Corporation. All rights reserved.
// ------------------------------------------------------------

#pragma warning disable AzDevAsync8 // An async version of this sync method exists
#pragma warning disable CA1819 // Properties should not return arrays

namespace Microsoft.Azure.DACS.Toolsets
{
    using System;
    using System.Collections.Generic;
    using System.Diagnostics;
    using System.IO;
    using System.Text;
    using Microsoft.Build.Framework;
    using Microsoft.Build.Utilities;

    public class UpdateMetadata : Task
    {
        private static readonly string[] MetadataPropertySeperator = new string[] { "," };
        private static readonly string[] MetadataPropertyValueSeperator = new string[] { "=" };
        private static readonly string[] AdditionalMetadataPropertySeperator = new string[] { ",," };
        private static readonly string[] AdditionalMetadataPropertyValueSeperator = new string[] { "==" };

        [Required]
        public ITaskItem[] TemplateFiles
        {
            get;
            set;
        }

        [Required]
        public ITaskItem[] GeneratedFiles
        {
            get;
            set;
        }

        [Required]
        public string MetadataDirectory
        {
            get;
            set;
        }

        [Required]
        public string MetadataProperties
        {
            get;
            set;
        }

        // additional metadata properties from the project
        public string AdditionalMetadataProperties
        {
            get;
            set;
        }

        public override bool Execute()
        {
            if (Environment.GetEnvironmentVariable("ATTACH_DEBUGGER_UpdateMetadata") != null)
            {
                Debugger.Launch();
            }

            try
            {
                Dictionary<string, string> metadataPropertyDictionary = this.GetMetadataProperties();

                for (int index = 0; index < this.TemplateFiles.Length; ++index)
                {
                    this.UpdateContentWithMetadata(this.TemplateFiles[index].GetMetadata("FullPath"), this.GeneratedFiles[index].GetMetadata("FullPath"), metadataPropertyDictionary);
                }
            }
            catch (Exception ex)
            {
                this.Log.LogErrorFromException(ex);
            }

            // return true if no errors
            return !this.Log.HasLoggedErrors;
        }

        private static void ParseAndAddMetadataProperties(
            IDictionary<string, string> propertyDictionary,
            string metadataProperties,
            string[] propertySeperator,
            string[] propertyValueSeperator)
        {
            if (string.IsNullOrEmpty(metadataProperties))
            {
                return;
            }

            string[] metadataParams = metadataProperties.Split(propertySeperator, StringSplitOptions.RemoveEmptyEntries);
            foreach (string metadataParam in metadataParams)
            {
                string[] metadataKeyValue = metadataParam.Split(propertyValueSeperator, StringSplitOptions.None);
                if (metadataKeyValue.Length > 1)
                {
                    propertyDictionary.Add(metadataKeyValue[0].Trim(), metadataKeyValue[1].Trim());
                }
            }
        }

        private void UpdateContentWithMetadata(string templateFilePath, string generatedFilePath, Dictionary<string, string> metadataPropertyDictionary)
        {
            string templateFileContent = File.ReadAllText(templateFilePath);

            while (true)
            {
                string originalValue = templateFileContent;

                foreach (KeyValuePair<string, string> kvp in metadataPropertyDictionary)
                {
                    string searchString = "$(" + kvp.Key + ")";
                    templateFileContent = templateFileContent.Replace(searchString, kvp.Value);
                }

                if (string.Equals(originalValue, templateFileContent, StringComparison.Ordinal))
                {
                    break;
                }
            }

            File.WriteAllText(generatedFilePath, templateFileContent);
        }

        private Dictionary<string, string> GetMetadataProperties()
        {
            Dictionary<string, string> propertyDictionary = new Dictionary<string, string>();

            string[] metadataFiles = Directory.GetFiles(this.MetadataDirectory, "*", SearchOption.AllDirectories);

            foreach (string metadataFile in metadataFiles)
            {
                using (StreamReader streamReader = new StreamReader(metadataFile))
                {
                    string line;
                    while ((line = streamReader.ReadLine()) != null)
                    {
                        int index = line.IndexOf('=');
                        if (index > 0)
                        {
                            string key = line.Substring(0, index);
                            string value = line.Substring(index + 1);
                            propertyDictionary.Add(key, value);
                        }
                    }
                }
            }

            ParseAndAddMetadataProperties(
                propertyDictionary,
                this.MetadataProperties,
                MetadataPropertySeperator,
                MetadataPropertyValueSeperator);

            ParseAndAddMetadataProperties(
                propertyDictionary,
                this.AdditionalMetadataProperties,
                AdditionalMetadataPropertySeperator,
                AdditionalMetadataPropertyValueSeperator);

            return propertyDictionary;
        }
    }
}
