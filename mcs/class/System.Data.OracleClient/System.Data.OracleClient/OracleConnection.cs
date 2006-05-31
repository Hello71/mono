//
// OracleConnection.cs 
//
// Part of the Mono class libraries at
// mcs/class/System.Data.OracleClient/System.Data.OracleClient
//
// Assembly: System.Data.OracleClient.dll
// Namespace: System.Data.OracleClient
//
// Authors: 
//    Daniel Morgan <danielmorgan@verizon.net>
//    Tim Coleman <tim@timcoleman.com>
//    Hubert FONGARNAND <informatique.internet@fiducial.fr>
//
// Copyright (C) Daniel Morgan, 2002, 2005, 2006
// Copyright (C) Tim Coleman, 2003
// Copyright (C) Hubert FONGARNAND, 2005
//
// Original source code for setting ConnectionString 
// by Tim Coleman <tim@timcoleman.com>
//
// Copyright (C) Tim Coleman, 2002
//
// Licensed under the MIT/X11 License.
//

using System;
using System.Collections;
using System.Collections.Specialized;
using System.ComponentModel;
using System.Data;
using System.Data.OracleClient.Oci;
using System.Drawing.Design;
using System.EnterpriseServices;
using System.Globalization;
using System.Text;

namespace System.Data.OracleClient 
{
	internal struct OracleConnectionInfo 
	{
		internal string Username;
		internal string Password;
		internal string Database;
		internal string ConnectionString;
		internal OciCredentialType CredentialType;
	}

	[DefaultEvent ("InfoMessage")]
	public sealed class OracleConnection : Component, ICloneable, IDbConnection
	{
		#region Fields

		OciGlue oci;
		ConnectionState state;
		OracleConnectionInfo conInfo;
		OracleTransaction transaction = null;
		string connectionString = String.Empty;
		string parsedConnectionString = String.Empty;
		OracleDataReader dataReader = null;
		bool pooling = true;
		static OracleConnectionPoolManager pools = new OracleConnectionPoolManager ();
		OracleConnectionPool pool;
		int minPoolSize = 0;
		int maxPoolSize = 100;
		byte persistSecurityInfo = 1;
		bool disposed = false;

		#endregion // Fields

		#region Constructors

		public OracleConnection () 
		{
			state = ConnectionState.Closed;
		}

		public OracleConnection (string connectionString) 
			: this() 
		{
			SetConnectionString (connectionString, false);
		}

		#endregion // Constructors

		#region Properties

		int IDbConnection.ConnectionTimeout {
			[MonoTODO]
			get { return -1; }
		}

		string IDbConnection.Database {
			[MonoTODO]
			get { return String.Empty; }
		}

		internal OracleDataReader DataReader {
			get { return dataReader; }
			set { dataReader = value; }
		}

		internal OciEnvironmentHandle Environment {
			get { return oci.Environment; }
		}

		internal OciErrorHandle ErrorHandle {
			get { return oci.ErrorHandle; }
		}

		internal OciServiceHandle ServiceContext {
			get { return oci.ServiceContext; }
		}

		internal OciSessionHandle Session {
			get { return oci.SessionHandle; }
		}

		[MonoTODO]
		[DesignerSerializationVisibility (DesignerSerializationVisibility.Hidden)]
		public string DataSource {
			get {
				return conInfo.Database;
			}
		}

		[Browsable (false)]
		[DesignerSerializationVisibility (DesignerSerializationVisibility.Hidden)]
		public ConnectionState State {
			get { return state; }
		}

		[DefaultValue ("")]
		[RecommendedAsConfigurable (true)]
		[RefreshProperties (RefreshProperties.All)]
		[Editor ("Microsoft.VSDesigner.Data.Oracle.Design.OracleConnectionStringEditor, " + Consts.AssemblyMicrosoft_VSDesigner, typeof(UITypeEditor))]
		public string ConnectionString {
			get { 
				return parsedConnectionString;
			}
			set { 
				SetConnectionString (value, false); 
			}
		}

		[MonoTODO]
		[Browsable (false)]
		[DesignerSerializationVisibility (DesignerSerializationVisibility.Hidden)]
		public string ServerVersion {
			get {
				if (this.State != ConnectionState.Open)
					throw new System.InvalidOperationException ("Invalid operation. The connection is closed.");
				return GetOracleVersion ();
			}
		}

		internal string GetOracleVersion () 
		{
			byte[] buffer = new Byte[256];
			uint bufflen = (uint) buffer.Length;

			IntPtr sh = oci.ServiceContext;
			IntPtr eh = oci.ErrorHandle;

			OciCalls.OCIServerVersion (sh, eh, ref buffer,  bufflen, OciHandleType.Service);
			
			// Get length of returned string
			int 	rsize = 0;
			IntPtr	env = oci.Environment;
			OciCalls.OCICharSetToUnicode (env, null, buffer, out rsize);
			
			// Get string
			StringBuilder ret = new StringBuilder(rsize);
			OciCalls.OCICharSetToUnicode (env, ret, buffer, out rsize);

			return ret.ToString ();
		}

		internal OciGlue Oci {
			get { return oci; }
		}

		internal OracleTransaction Transaction {
			get { return transaction; }
			set { transaction = value; }
		}

		#endregion // Properties

		#region Methods

		public OracleTransaction BeginTransaction ()
		{
			return BeginTransaction (IsolationLevel.ReadCommitted);
		}

		public OracleTransaction BeginTransaction (IsolationLevel il)
		{
			if (state == ConnectionState.Closed)
				throw new InvalidOperationException ("The connection is not open.");
			if (transaction != null)
				throw new InvalidOperationException ("OracleConnection does not support parallel transactions.");

			OciTransactionHandle transactionHandle = oci.CreateTransaction ();
			if (transactionHandle == null) 
				throw new Exception("Error: Unable to start transaction");
			else {
				transactionHandle.Begin ();
				transaction = new OracleTransaction (this, il, transactionHandle);
			}

			return transaction;
		}

		[MonoTODO]
		void IDbConnection.ChangeDatabase (string databaseName)
		{
			throw new NotImplementedException ();
		}

		public OracleCommand CreateCommand ()
		{
			OracleCommand command = new OracleCommand ();
			command.Connection = this;
			return command;
		}

		[MonoTODO]
		object ICloneable.Clone ()
		{
			OracleConnection con = new OracleConnection ();
			con.SetConnectionString (connectionString, true);
			// TODO: what other properties need to be cloned?
			return con;
		}

		IDbTransaction IDbConnection.BeginTransaction ()
		{
			return BeginTransaction ();
		}

		IDbTransaction IDbConnection.BeginTransaction (IsolationLevel iso)
		{
			return BeginTransaction (iso);
		}

		IDbCommand IDbConnection.CreateCommand ()
		{
			return CreateCommand ();
		}

		void IDisposable.Dispose ()
		{
			Dispose (true);
			GC.SuppressFinalize (this);
		}

		[MonoTODO]
		protected override void Dispose (bool disposing)
		{
			if (!disposed) {
				if (State == ConnectionState.Open)
					Close ();
				dataReader = null;
				transaction = null;
				oci = null;
				pool = null;
				conInfo.Username = "";
				conInfo.Database = "";
				conInfo.Password = "";
				connectionString = "";
				parsedConnectionString = "";
				base.Dispose (disposing);
				disposed = true;
			}
		}

		[MonoTODO]
		public void EnlistDistributedTransaction (ITransaction distributedTransaction)
		{
			throw new NotImplementedException ();
		}

		// Get NLS_DATE_FORMAT string from Oracle server
		internal string GetSessionDateFormat () 
		{
			// 23 is 22 plus 1 for NUL terminated character
			// a DATE format has a max size of 22
			return GetNlsInfo (Session, 23, OciNlsServiceType.DATEFORMAT);
		}

		// Get NLS Info
		//
		// handle = OciEnvironmentHandle or OciSessionHandle
		// bufflen = Length of byte buffer to allocate to retrieve the NLS info
		// item = OciNlsServiceType enum value
		//
		// if unsure how much you need, use OciNlsServiceType.MAXBUFSZ
		internal string GetNlsInfo (OciHandle handle, uint bufflen, OciNlsServiceType item) 
		{
			byte[] buffer = new Byte[bufflen];

			OciCalls.OCINlsGetInfo (handle, ErrorHandle, 
				ref buffer, bufflen, (ushort) item);

			// Get length of returned string
			int rsize = 0;
			OciCalls.OCICharSetToUnicode (Environment, null, buffer, out rsize);
			
			// Get string
			StringBuilder ret = new StringBuilder (rsize);
			OciCalls.OCICharSetToUnicode (Environment, ret, buffer, out rsize);

			return ret.ToString ();
		}

		public void Open () 
		{
			PersistSecurityInfo ();

			if (!pooling) {	
				oci = new OciGlue ();
				oci.CreateConnection (conInfo);
			}
			else {
				pool = pools.GetConnectionPool (conInfo, minPoolSize, maxPoolSize);
				oci = pool.GetConnection ();
			}
			state = ConnectionState.Open;
			CreateStateChange (ConnectionState.Closed, ConnectionState.Open);
		}

		internal void CreateInfoMessage (OciErrorInfo info) 
		{
			OracleInfoMessageEventArgs a = new OracleInfoMessageEventArgs (info);
			OnInfoMessage (a);
		}

		private void OnInfoMessage (OracleInfoMessageEventArgs e) 
		{
			if (InfoMessage != null)
				InfoMessage (this, e);
		}

		internal void CreateStateChange (ConnectionState original, ConnectionState current) 
		{
			StateChangeEventArgs a = new StateChangeEventArgs (original, current);
			OnStateChange (a);
		}

		private void OnStateChange (StateChangeEventArgs e) 
		{
			if (StateChange != null)
				StateChange (this, e);
		}

		public void Close () 
		{
			if (transaction != null)
				transaction.Rollback ();

			if (!pooling)
				oci.Disconnect ();
			else if (pool != null)
				pool.ReleaseConnection (oci);

			state = ConnectionState.Closed;
			CreateStateChange (ConnectionState.Open, ConnectionState.Closed);
		}

		private void PersistSecurityInfo () 
		{
			// persistSecurityInfo:
			// 0 = true/yes
			// 1 = false/no (have not parsed out password yet)
			// 2 = like 1, but have parsed out password

			if (persistSecurityInfo == 0 || persistSecurityInfo == 2)
				return;

			persistSecurityInfo = 2;

			if (connectionString == null)
				return;

			if (connectionString == String.Empty)
				return;
			
			string conString = connectionString + ";";

			bool inQuote = false;
			bool inDQuote = false;

			string name = String.Empty;
			StringBuilder sb = new StringBuilder ();
			int nStart = 0;
			int nFinish = 0;
			int i = -1;

			foreach (char c in conString) {
				i ++;

				switch (c) {
				case '\'':
					inQuote = !inQuote;
					break;
				case '"' :
					inDQuote = !inDQuote;
					break;
				case ';' :
					if (!inDQuote && !inQuote) {
						if (name != String.Empty && name != null) {
							name = name.ToUpper ().Trim ();
							if (name.Equals ("PASSWORD") || name.Equals ("PWD")) {
								nFinish = i;
								string part1 = String.Empty;
								string part3 = String.Empty;
								sb = new StringBuilder ();
								if (nStart > 0) {
									part1 = conString.Substring (0, nStart);
									if (part1[part1.Length - 1] == ';')
										part1 = part1.Substring (0, part1.Length - 1);
									sb.Append (part1);
								}
								if (!part1.Equals (String.Empty))
									sb.Append (';');
								if (conString.Length - nFinish - 1 > 0) {
									part3 = conString.Substring (nFinish, conString.Length - nFinish);
									if (part3[0] == ';')  
										part3 = part3.Substring(1, part3.Length - 1);
									sb.Append (part3);
								}
								parsedConnectionString = sb.ToString ();
								return;
							}
						}
						name = String.Empty;
						sb = new StringBuilder ();
						nStart = i;
						nFinish = i;
					}
					else
						sb.Append (c);
					break;
				case '=' :
					if (!inDQuote && !inQuote) {
						name = sb.ToString ();
						sb = new StringBuilder ();
					}
					else
						sb.Append (c);
					break;
				default:
					sb.Append (c);
					break;
				}
			}
		}

		internal void SetConnectionString (string connectionString, bool persistSecurity) 
		{
			persistSecurityInfo = 1;
			this.connectionString = String.Copy (connectionString);
			this.parsedConnectionString = this.connectionString;
			if (this.connectionString == null)
				this.connectionString = String.Empty;
			conInfo.Username = "";
			conInfo.Database = "";
			conInfo.Password = "";
			conInfo.CredentialType = OciCredentialType.RDBMS;

			if (connectionString == null)
				return;

			if (connectionString == String.Empty)
				return;
			
			connectionString += ";";
			NameValueCollection parameters = new NameValueCollection ();

			bool inQuote = false;
			bool inDQuote = false;

			string name = String.Empty;
			string value = String.Empty;
			StringBuilder sb = new StringBuilder ();

			foreach (char c in connectionString) {
				switch (c) {
				case '\'':
					inQuote = !inQuote;
					break;
				case '"' :
					inDQuote = !inDQuote;
					break;
				case ';' :
					if (!inDQuote && !inQuote) {
						if (name != String.Empty && name != null) {
							name = name.ToUpper ().Trim ();
							value = sb.ToString ().Trim ();
							parameters [name] = value;
						}
						name = String.Empty;
						value = String.Empty;
						sb = new StringBuilder ();
					}
					else
						sb.Append (c);
					break;
				case '=' :
					if (!inDQuote && !inQuote) {
						name = sb.ToString ();
						sb = new StringBuilder ();
					}
					else
						sb.Append (c);
					break;
				default:
					sb.Append (c);
					break;
				}
			}

			SetProperties (parameters);

			conInfo.ConnectionString = this.connectionString;

			if (persistSecurity == true)
				PersistSecurityInfo ();
		}

		private void SetProperties (NameValueCollection parameters) 
		{	
			string value;
			foreach (string name in parameters) {
				value = parameters[name];

				switch (name) {
				case "UNICODE":
					break;
				case "ENLIST":
					break;
				case "CONNECTION LIFETIME":
					// TODO:
					break;
				case "INTEGRATED SECURITY":
					if (ConvertToBoolean ("integrated security", value) == false)
						conInfo.CredentialType = OciCredentialType.RDBMS;
					else
						conInfo.CredentialType = OciCredentialType.External;
					break;
				case "PERSIST SECURITY INFO":
					if (ConvertToBoolean ("persist security info", value) == false)
						persistSecurityInfo = 1;
					else
						persistSecurityInfo = 0;
					break;
				case "MIN POOL SIZE":
					minPoolSize = int.Parse (value);
					break;
				case "MAX POOL SIZE":
					maxPoolSize = int.Parse (value);
					break;
				case "DATA SOURCE" :
				case "SERVER" :
					conInfo.Database = value;
					break;
				case "PASSWORD" :
				case "PWD" :
					conInfo.Password = value;
					break;
				case "UID" :
				case "USER ID" :
					conInfo.Username = value;
					break;
				case "POOLING" :
					pooling = ConvertToBoolean("pooling", value);
					break;
				default:
					throw new ArgumentException("Connection parameter not supported: '" + name + "'");
				}
			}
		}

		private bool ConvertToBoolean(string key, string value) 
		{
			string upperValue = value.ToUpper();

			if (upperValue == "TRUE" ||upperValue == "YES") {
				return true;
			} 
			else if (upperValue == "FALSE" || upperValue == "NO") {
				return false;
			}

			throw new ArgumentException(string.Format(CultureInfo.InvariantCulture,
				"Invalid value \"{0}\" for key '{1}'.", value, key));
		}


		#endregion // Methods

		public event OracleInfoMessageEventHandler InfoMessage;
		public event StateChangeEventHandler StateChange;
	}
}
