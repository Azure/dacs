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

    public class AssembleDockerfile : Task
    {
        private const string WarningHeader = @"
# =============================================================================
#
# THIS IS A GENERATED DOCKERFILE - DO NOT MODIFY
#
# Make changes to Dockerfile.template 
#
# =============================================================================
";

        private static readonly string[] ReplacementPropertySeperator = new string[] { ",," };
        private static readonly string[] ReplacementPropertyValueSeperator = new string[] { "==" };

        [Required]
        public string InputDockerTemplateFileName
        {
            get;
            set;
        }

        [Required]
        public string OutputDockerFileName
        {
            get;
            set;
        }

        public string ReplacementProperties
        {
            get;
            set;
        }

        public override bool Execute()
        {
            if (Environment.GetEnvironmentVariable("ATTACH_DEBUGGER_AssembleDockerfile") != null)
            {
                Debugger.Launch();
            }

            try
            {
                Dictionary<string, string> replacementProperties = this.GetReplacementProperties();
                this.Assemble(
                    this.InputDockerTemplateFileName,
                    this.OutputDockerFileName,
                    replacementProperties);
            }
            catch (Exception ex)
            {
                this.Log.LogErrorFromException(ex);
            }

            // return true if no errors
            return !this.Log.HasLoggedErrors;
        }

        private void Assemble(
            string templateFilePath,
            string generatedFilePath,
            Dictionary<string, string> replacementProperties)
        {
            string dockerFileContent = WarningHeader + this.ProcessIncludes(templateFilePath, replacementProperties);
            dockerFileContent = dockerFileContent.Replace("\r\n", "\n") + "\n";
            File.WriteAllText(generatedFilePath, dockerFileContent, Encoding.Default);
        }

        private string ProcessIncludes(
            string templateFilePath,
            Dictionary<string, string> replacementProperties)
        {
            templateFilePath = Path.GetFullPath(templateFilePath);
            string[] inputDockerTemplateContents = File.ReadAllLines(templateFilePath);
            string inputDockerTemplateFileDirectory = Path.GetDirectoryName(templateFilePath);
            string fileContent = string.Empty;

            foreach (string l in inputDockerTemplateContents)
            {
                string lineContent = this.PerformanceReplacements(l, replacementProperties);

                if (lineContent.StartsWith("INCLUDE", StringComparison.Ordinal))
                {
                    string includeFile = lineContent.Substring("INCLUDE".Length).Trim();
                    string includeFilePath = Path.Combine(inputDockerTemplateFileDirectory, includeFile);

                    fileContent += this.ProcessIncludes(includeFilePath, replacementProperties);
                }
                else
                {
                    fileContent = fileContent + lineContent + "\n";
                }
            }

            return fileContent;
        }

        private string PerformanceReplacements(
            string line,
            Dictionary<string, string> replacementProperties)
        {
            if (line.IndexOf("[[", StringComparison.Ordinal) != -1)
            {
                // perform replacements
                foreach (string key in replacementProperties.Keys)
                {
                    string token = $"[[{key}]]";
                    line = line.Replace(token, replacementProperties[key]);
                }
            }

            return line;
        }

        private Dictionary<string, string> GetReplacementProperties()
        {
            // add default replacement properties
            Dictionary<string, string> replacementProperties = new Dictionary<string, string>();
            replacementProperties.Add("CRIU", "criu");
            replacementProperties.Add("UBUNTU_VERSION", "18.04");
            replacementProperties.Add("IMAGE_SPECIFIC_FILENAME_SUFFIX", string.Empty);

            this.ParseAndAddReplacementProperties(
                replacementProperties,
                this.ReplacementProperties,
                ReplacementPropertySeperator,
                ReplacementPropertyValueSeperator);

            return replacementProperties;
        }

        private void ParseAndAddReplacementProperties(
            IDictionary<string, string> propertyDictionary,
            string replacementProperties,
            string[] propertySeperator,
            string[] propertyValueSeperator)
        {
            if (string.IsNullOrEmpty(replacementProperties))
            {
                return;
            }

            string[] metadataParams = replacementProperties.Split(propertySeperator, StringSplitOptions.RemoveEmptyEntries);
            foreach (string metadataParam in metadataParams)
            {
                string[] metadataKeyValue = metadataParam.Split(propertyValueSeperator, StringSplitOptions.None);
                if (metadataKeyValue.Length > 1)
                {
                    propertyDictionary[metadataKeyValue[0].Trim()] = metadataKeyValue[1].Trim();
                }
            }
        }
    }
}
